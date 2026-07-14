#pragma once

#include <string>
#include <vector>

namespace Wiki
{
    struct SanitizedHtml
    {
        std::string articleHtml;
        std::string railHtml;
        std::string railTitle;
        std::vector<std::string> skippedWidgets;
        std::vector<std::string> imageUrls;   // resolved, host-allowed article <img> srcs -> prefetch on page load
    };

    SanitizedHtml SanitizeArticleHtml(const std::string& html, const std::string& title);
    std::string ResolveWikiUrl(const std::string& url, const std::string& baseUrl = "https://wiki.guildwars2.com/wiki/");
}
