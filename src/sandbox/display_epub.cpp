#include "doc_api/token_addressing.h"
#include "epub/epub_reader.h"
#include "reader/display_lines.h"

#include <iostream>
#include <iomanip>
#include <string>

static bool fits_on_line_by_char(const char *, uint32_t strlen)
{
    return strlen <= 80;
}

void display_epub(std::string path)
{
    std::cout << "================================" << std::endl;
    std::cout << path << std::endl;

    EPubReader epub(path);
    if (!epub.open())
    {
        std::cerr << "Unable to open epub" << std::endl;
        return;
    }

    // Display toc
    const auto &toc = epub.get_table_of_contents();
    for (uint32_t i = 0; i < toc.size(); ++i)
    {
        const auto &toc_item = toc[i];
        std::cout << std::string(toc_item.indent_level * 2, ' ') << toc_item.display_name <<  std::endl;

        auto toc_addr = epub.get_toc_item_address(i);
        if (get_text_number(toc_addr) > 0)
        {
            const auto &tokens = epub.load_chapter(toc_addr);
            bool found_matching_addr = false;
            for (const auto &token : tokens)
            {
                if (toc_addr == token.address)
                {
                    found_matching_addr = true;
                    break;
                }
            }
            if (!found_matching_addr)
            {
                std::cerr << "Exact match for toc " << toc_item.display_name << " with address " << to_string(toc_addr) << " not found" << std::endl;
            }
        }
    }

    // Display book
    DocAddr addr = epub.get_first_chapter_address();
    TocPosition last_progress {0, 0};
    while (true)
    {
        std::cout << "--------------------------------" << std::endl;
        uint32_t toc_index = epub.get_toc_position(addr).toc_index;
        std::cout << "["
                  << toc_index << ": "
                  << (toc_index < toc.size() ? toc[toc_index].display_name : "null")
                  << "]" << std::endl;

        const auto &tokens = epub.load_chapter(addr);
        std::vector<Line> display_lines = get_display_lines(
            tokens,
            fits_on_line_by_char
        );

        for (const auto &line: display_lines)
        {
            auto progress = epub.get_toc_position(line.address);
            if (progress.toc_index < last_progress.toc_index)
            {
                std::cerr << "Toc went backwards for address " << to_string(line.address) << std::endl;
            }
            else if (progress.toc_index == last_progress.toc_index && progress.progress_percent < last_progress.progress_percent)
            {
                std::cerr << "Progress went backwards for address " << to_string(line.address)
                          << " (" << last_progress.progress_percent << " -> " << progress.progress_percent << ")" << std::endl;
            }

            last_progress = progress;

            std::cout << to_string(line.address) << " ";
            std::cout << std::setw(2) << progress.toc_index << " ";
            std::cout << std::setw(3) << progress.progress_percent;
            std::cout << "% | " << line.text << std::endl;
        }

        auto next_addr = epub.get_next_chapter_address(addr);
        if (next_addr)
        {
            addr = *next_addr;
        }
        else
        {
            break;
        }
    }
}
