#pragma once

#include <cstdint>

namespace launcher::Icons {

struct IconGlyph {
    char utf8[5]{};

    constexpr explicit IconGlyph(std::uint32_t codepoint)
    {
        if (codepoint <= 0x7f) {
            utf8[0] = static_cast<char>(codepoint);
        } else if (codepoint <= 0x7ff) {
            utf8[0] = static_cast<char>(0xc0 | (codepoint >> 6));
            utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3f));
        } else if (codepoint <= 0xffff) {
            utf8[0] = static_cast<char>(0xe0 | (codepoint >> 12));
            utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3f));
        } else {
            utf8[0] = static_cast<char>(0xf0 | (codepoint >> 18));
            utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
            utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3f));
        }
    }

    constexpr operator const char*() const
    {
        return utf8;
    }
};

inline constexpr IconGlyph Add{0xe145};
inline constexpr IconGlyph Back{0xe5c4};
inline constexpr IconGlyph BatchOperation{0xf0c5}; // fact_check
inline constexpr IconGlyph BuildInfo{0xe88e};      // info
inline constexpr IconGlyph CenterWindow{0xe3b4};   // center_focus_strong
inline constexpr IconGlyph Check{0xe5ca};
inline constexpr IconGlyph Close{0xe5cd};
inline constexpr IconGlyph Copy{0xe14d};           // content_copy
inline constexpr IconGlyph CopyPath{0xe14d};       // content_copy
inline constexpr IconGlyph CopyProperties{0xe873}; // description
inline constexpr IconGlyph Cut{0xe14e};            // content_cut
inline constexpr IconGlyph DataManagement{0xead3}; // data_object
inline constexpr IconGlyph Delete{0xe872};
inline constexpr IconGlyph DoNotDisturb{0xe612}; // do_not_disturb
inline constexpr IconGlyph Download{0xe2c4};
inline constexpr IconGlyph Edit{0xe3c9};
inline constexpr IconGlyph EmptyItem{0xe835}; // check_box_outline_blank
inline constexpr IconGlyph Folder{0xe2c7};
inline constexpr IconGlyph FolderOpen{0xe2c8}; // folder_open
inline constexpr IconGlyph Forward{0xe5c8};
inline constexpr IconGlyph Help{0xe887};
inline constexpr IconGlyph Home{0xe88a};
inline constexpr IconGlyph Image{0xe3f4};
inline constexpr IconGlyph Keyboard{0xe312};
inline constexpr IconGlyph Layout{0xe8f0}; // view_module
inline constexpr IconGlyph Link{0xe157};
inline constexpr IconGlyph Menu{0xe5d2};
inline constexpr IconGlyph MinimizeRun{0xe931}; // minimize
inline constexpr IconGlyph Note{0xf1fc};        // sticky_note_2
inline constexpr IconGlyph OpenWith{0xe89f};
inline constexpr IconGlyph Paste{0xe14f};       // content_paste
inline constexpr IconGlyph Pin{0xf10d};         // push_pin
inline constexpr IconGlyph Placeholder{0xe835}; // check_box_outline_blank
inline constexpr IconGlyph Plugin{0xe87b};
inline constexpr IconGlyph QuickSettings{0xe429}; // tune
inline constexpr IconGlyph Refresh{0xe5d5};
inline constexpr IconGlyph Rename{0xe3c9};
inline constexpr IconGlyph RunAsAdmin{0xef3d}; // admin_panel_settings
inline constexpr IconGlyph RunAll{0xe037};     // play_arrow
inline constexpr IconGlyph Script{0xe86f};     // code
inline constexpr IconGlyph Search{0xe8b6};
inline constexpr IconGlyph Settings{0xe8b8};
inline constexpr IconGlyph Sort{0xe164};
inline constexpr IconGlyph Task{0xf075};
inline constexpr IconGlyph Theme{0xe40a}; // palette
inline constexpr IconGlyph Title{0xe264};
inline constexpr IconGlyph Tools{0xe869};   // build
inline constexpr IconGlyph TopMost{0xe25a}; // vertical_align_top
inline constexpr IconGlyph Update{0xe923};

} // namespace launcher::Icons
