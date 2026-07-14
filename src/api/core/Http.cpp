#include "Http.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <mutex>
#include <set>

namespace Api::Http
{
    namespace
    {
        // One shared session for the addon's lifetime (cheap to reuse; per-request connections are opened
        // under it). s_sessionMtx guards the session AND the in-flight request set below. Multiple workers
        // (the API pool + the ImageCache worker) call Get concurrently -- WinHTTP allows concurrent requests
        // on one session.
        constexpr int         kScopes = (int)Scope::Count;
        std::mutex            s_sessionMtx;
        HINTERNET             s_session = nullptr;
        // Per-scope abort state + in-flight request handles (all guarded by s_sessionMtx). A scope's Abort() only
        // touches ITS row, so one subsystem's teardown can't cancel another's requests.
        bool                            s_scopeAborting[kScopes] = {};
        std::array<std::set<HINTERNET>, kScopes> s_scopeReqs;
        int                   s_abortCount = 0;      // diagnostics: total Abort()/Resume() calls (guarded by s_sessionMtx)
        int                   s_resumeCount = 0;

        HINTERNET Session()   // caller holds s_sessionMtx. Scope-agnostic: aborting is enforced per-scope in Get().
        {
            if (!s_session)
            {
                s_session = WinHttpOpen(L"TyrianCodex",
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (s_session)
                {
                    // resolve / connect / send / receive timeouts (ms): keep a stuck request from hanging
                    // the worker for long. API downtime then surfaces as a Network error, not a freeze.
                    WinHttpSetTimeouts(s_session, 8000, 8000, 15000, 20000);
                    // Allow the worker pool to open several concurrent connections to the SAME host (the API
                    // pool all hits api.guildwars2.com) so requests actually overlap instead of serializing.
                    DWORD maxConns = 8;
                    WinHttpSetOption(s_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &maxConns, sizeof(maxConns));
                    WinHttpSetOption(s_session, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &maxConns, sizeof(maxConns));
                }
            }
            return s_session;
        }

        std::wstring Widen(const std::string& s)
        {
            if (s.empty()) return std::wstring();
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        std::string Narrow(const wchar_t* s, int len)
        {
            if (!s || len <= 0) return std::string();
            int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
            std::string o(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, s, len, &o[0], n, nullptr, nullptr);
            return o;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)::tolower((unsigned char)c);
            return s;
        }

        // Parse the WINHTTP_QUERY_RAW_HEADERS_CRLF block ("HTTP/1.1 200 OK\r\nName: Value\r\n...") into a
        // lowercased name -> value map (skips the status line).
        void ParseHeaders(const std::wstring& raw, std::map<std::string, std::string>& out)
        {
            size_t start = 0;
            bool firstLine = true;
            while (start < raw.size())
            {
                size_t end = raw.find(L"\r\n", start);
                if (end == std::wstring::npos) end = raw.size();
                std::wstring line = raw.substr(start, end - start);
                start = end + 2;
                if (firstLine) { firstLine = false; continue; }   // status line
                if (line.empty()) continue;
                size_t colon = line.find(L':');
                if (colon == std::wstring::npos) continue;
                std::wstring name = line.substr(0, colon);
                size_t vstart = colon + 1;
                while (vstart < line.size() && (line[vstart] == L' ' || line[vstart] == L'\t')) ++vstart;
                std::wstring value = line.substr(vstart);
                out[Lower(Narrow(name.c_str(), (int)name.size()))] = Narrow(value.c_str(), (int)value.size());
            }
        }
    }

    RawResponse Get(const std::string& url,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    Scope scope)
    {
        RawResponse r;
        const int sc = (int)scope;

        // Lock ONLY the lazy session open, NOT the whole request. WinHTTP allows concurrent requests on a
        // single session handle (each Get below opens its own conn/req handles), so the tile-cache worker and
        // the GW2-API worker stop serializing on each other - a tile-prefetch flood at login no longer starves
        // the Journal's achievement/reward fetches (they'd wait behind hundreds of tiles -> "stuck loading").
        // Safe at teardown: AddonUnload JOINS both workers (ImageCache::Shutdown + api.Shutdown) before
        // Http::Shutdown closes s_session, so no request is ever in flight when the handle is destroyed.
        HINTERNET session;
        { std::lock_guard<std::mutex> lock(s_sessionMtx); session = Session(); }
        if (!session) { r.networkError = true; r.errorText = "WinHttpOpen failed"; return r; }

        // Split the URL into host / path+query / scheme via WinHttpCrackUrl (pointer mode: it returns spans
        // into our wide buffer, no copies).
        std::wstring wurl = Widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize      = sizeof(uc);
        uc.dwHostNameLength  = (DWORD)-1;
        uc.dwUrlPathLength   = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;
        uc.dwSchemeLength    = (DWORD)-1;
        if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        { r.networkError = true; r.errorText = "WinHttpCrackUrl failed"; return r; }

        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

        HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
        if (!conn) { r.networkError = true; r.errorText = "WinHttpConnect failed"; return r; }

        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(conn); r.networkError = true; r.errorText = "WinHttpOpenRequest failed"; return r; }

        // Register the in-flight request so Abort() can cancel it (a concurrent WinHttpCloseHandle makes the
        // blocking send/receive below return at once). Whoever removes it from the set owns the close, so the
        // request handle is never closed twice.
        {
            std::lock_guard<std::mutex> lock(s_sessionMtx);
            if (s_scopeAborting[sc]) { WinHttpCloseHandle(req); WinHttpCloseHandle(conn); r.networkError = true; r.errorText = "aborted"; return r; }
            s_scopeReqs[sc].insert(req);
        }
        auto closeReq = [&]() {
            std::lock_guard<std::mutex> lock(s_sessionMtx);
            auto it = s_scopeReqs[sc].find(req);
            if (it != s_scopeReqs[sc].end()) { s_scopeReqs[sc].erase(it); WinHttpCloseHandle(req); }   // else Abort() already closed it
        };

        // Combine the extra request headers into one CRLF block.
        std::wstring hdrBlock;
        for (const auto& kv : headers)
            hdrBlock += Widen(kv.first) + L": " + Widen(kv.second) + L"\r\n";
        if (!hdrBlock.empty())
            WinHttpAddRequestHeaders(req, hdrBlock.c_str(), (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        bool sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                 && WinHttpReceiveResponse(req, nullptr);
        if (!sent)
        {
            r.networkError = true; r.errorText = "WinHttpSendRequest/Receive failed";
            closeReq(); WinHttpCloseHandle(conn);
            return r;
        }

        // Status code.
        DWORD code = 0, codeSize = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
        r.status = (long)code;

        // Raw response headers (for Cache-Control / Expires / X-Page-* etc.).
        DWORD hdrSize = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER, &hdrSize, WINHTTP_NO_HEADER_INDEX);
        if (hdrSize > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            std::wstring rawHdr(hdrSize / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &rawHdr[0], &hdrSize, WINHTTP_NO_HEADER_INDEX))
                ParseHeaders(rawHdr, r.headers);
        }

        // Body.
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            size_t off = r.body.size();
            r.body.resize(off + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, &r.body[off], avail, &read)) { r.body.resize(off); break; }
            r.body.resize(off + read);
            if (read == 0) break;
        }

        closeReq();
        WinHttpCloseHandle(conn);
        return r;
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(s_sessionMtx);
        if (s_session) { WinHttpCloseHandle(s_session); s_session = nullptr; }
    }

    void Abort(Scope scope)
    {
        const int sc = (int)scope;
        std::lock_guard<std::mutex> lock(s_sessionMtx);
        s_scopeAborting[sc] = true;
        ++s_abortCount;
        for (HINTERNET h : s_scopeReqs[sc]) WinHttpCloseHandle(h);   // unblocks this scope's workers mid-request
        s_scopeReqs[sc].clear();
    }

    void Resume(Scope scope)
    {
        std::lock_guard<std::mutex> lock(s_sessionMtx);
        s_scopeAborting[(int)scope] = false;
        ++s_resumeCount;
    }

    DiagState Diag()
    {
        std::lock_guard<std::mutex> lock(s_sessionMtx);
        DiagState d;
        d.apiAborting = s_scopeAborting[(int)Scope::Api];
        d.sessionOpen = (s_session != nullptr);
        d.abortCount  = s_abortCount;
        d.resumeCount = s_resumeCount;
        for (const auto& reqs : s_scopeReqs) d.activeReqs += (int)reqs.size();
        return d;
    }
}
