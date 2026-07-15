#pragma once

#include "imgui.h"
#include <litehtml.h>
#include <memory>
#include <string>
#include <vector>

namespace Wiki
{
    // Native litehtml drawing bridge. litehtml owns HTML parsing, CSS cascade, and box layout;
    // this container supplies Tyrian Codex text/images/backgrounds/borders through ImGui.
    class Container final : public litehtml::document_container
    {
    public:
        Container();

        void SetOrigin(ImVec2 origin) { m_origin = origin; }
        void SetViewport(ImVec2 size) { m_viewport = size; }
        void SetBaseUrl(std::string url) { m_baseUrl = std::move(url); }
        void SetMinimumFontSize(float px) { m_minFontSize = px; }
        void SetContentScale(float scale);
        void ResetFrame();

        std::string TakeClickedUrl();
        std::string TakeClickedImageUrl();
        const std::string& Tooltip() const { return m_tooltip; }

        litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc,
                                       litehtml::font_metrics* fm) override;
        void delete_font(litehtml::uint_ptr hFont) override;
        litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
        void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
                       litehtml::web_color color, const litehtml::position& pos) override;
        litehtml::pixel_t pt_to_px(float pt) const override;
        litehtml::pixel_t get_default_font_size() const override;
        const char* get_default_font_name() const override;
        void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
        void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
        void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
        void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                        const std::string& url, const std::string& base_url) override;
        void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                             const litehtml::web_color& color) override;
        void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::linear_gradient& gradient) override;
        void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::radial_gradient& gradient) override;
        void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                 const litehtml::background_layer::conic_gradient& gradient) override;
        void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders,
                          const litehtml::position& draw_pos, bool root) override;

        void set_caption(const char* caption) override;
        void set_base_url(const char* base_url) override;
        void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override;
        void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
        void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;
        void set_cursor(const char* cursor) override;
        void transform_text(litehtml::string& text, litehtml::text_transform tt) override;
        void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override;
        void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
        void del_clip() override;
        void get_viewport(litehtml::position& viewport) const override;
        litehtml::element::ptr create_element(const char* tag_name,
                                              const litehtml::string_map& attributes,
                                              const std::shared_ptr<litehtml::document>& doc) override;
        void get_media_features(litehtml::media_features& media) const override;
        void get_language(litehtml::string& language, litehtml::string& culture) const override;
        litehtml::string resolve_color(const litehtml::string& color) const override;

    private:
        struct FontRef
        {
            ImFont* font = nullptr;
            float size = 16.f;
            int weight = 400;
            bool italic = false;
            int decorationLine = 0;
            litehtml::font_metrics metrics;
        };

        struct LayerGeometry
        {
            ImVec2 borderMin;
            ImVec2 borderMax;
            ImVec2 clipMin;
            ImVec2 clipMax;
            float tl = 0.f;
            float tr = 0.f;
            float br = 0.f;
            float bl = 0.f;
        };

        static FontRef* FontFromHandle(litehtml::uint_ptr hFont);
        static float SeparatorAdvance(const FontRef* font);
        static float TextWidthWithSeparators(const char* text, const FontRef* font);
        static void DrawTextWithSeparators(ImDrawList* dl, const FontRef* font, ImVec2 pos, ImU32 col,
                                           const char* text, bool bold);
        ImVec2 ScreenPos(const litehtml::position& pos) const;
        LayerGeometry Geometry(const litehtml::background_layer& layer) const;
        std::string ResolveResourceUrl(const char* src, const char* baseurl) const;
        std::string ResolveResourceUrl(const std::string& src, const std::string& baseurl) const;
        std::string ImageTextureId(const std::string& url) const;
        std::string FindImageUrl(const litehtml::element::ptr& el) const;
        static std::string OriginalImageUrl(const std::string& url);
        static ImU32 Color(litehtml::web_color c);
        static litehtml::web_color FirstGradientColor(const std::vector<litehtml::background_layer::color_point>& points);
        void DrawUniformBorder(ImDrawList* dl, const litehtml::borders& borders, ImVec2 a, ImVec2 b);
        void DrawSideBorder(ImDrawList* dl, const litehtml::border& border, ImVec2 a, ImVec2 b) const;

        ImVec2 m_origin = ImVec2(0.f, 0.f);
        ImVec2 m_viewport = ImVec2(900.f, 650.f);
        float m_minFontSize = 16.f;   // floor (was 12) -- no wiki text smaller/blurrier than a clean 16px
        float m_contentScale = 1.f;    // scales litehtml text/image layout through real relayout, not stretched output
        std::string m_baseUrl = "https://wiki.guildwars2.com/wiki/";
        std::string m_caption;
        std::string m_clickedUrl;
        std::string m_clickedImageUrl;
        std::string m_tooltip;
        std::vector<std::unique_ptr<FontRef>> m_fonts;
        int m_clipDepth = 0;
    };

    std::shared_ptr<litehtml::document> CreateDocument(const std::string& bodyHtml,
                                                       const std::string& userCss,
                                                       Container& container);
}
