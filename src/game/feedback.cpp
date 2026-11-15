#include "game/feedback.h"

#include <cctype>
#include <cstdio>

namespace game {

const char* feedback_region(float x, float y) {
    static const char* const names[3][3] = {
        {"top left", "top centre", "top right"},
        {"centre left", "centre", "centre right"},
        {"bottom left", "bottom centre", "bottom right"},
    };
    const int column = x < 1.0f / 3.0f ? 0 : (x < 2.0f / 3.0f ? 1 : 2);
    const int row = y < 1.0f / 3.0f ? 0 : (y < 2.0f / 3.0f ? 1 : 2);
    return names[row][column];
}

std::string feedback_markdown(const FeedbackReport& report) {
    std::string out = "# Feedback";
    if (!report.when.empty()) out += ", " + report.when;
    out += "\n\n";
    out += report.summary.empty() ? "(no summary)" : report.summary;
    out += "\n\n";
    out += "![the frame with its marks](" + report.marked_file + ")\n\n";
    out += "The frame with nothing drawn on it is `" + report.frame_file + "`.\n\n";

    out += "## Marks\n\n";
    if (report.marks.empty()) {
        out += "None. The note is about the whole frame.\n\n";
    } else {
        out += "| # | Where | x | y | Note |\n|---|---|---|---|---|\n";
        char line[96];
        int number = 1;
        for (const FeedbackMark& mark : report.marks) {
            std::snprintf(line, sizeof(line), "| %d | %s | %.3f | %.3f | ", number, feedback_region(mark.x, mark.y),
                          double(mark.x), double(mark.y));
            out += line;
            // A table cell is one line and has no bar in it.
            for (char c : mark.note) out += c == '\n' ? ' ' : (c == '|' ? '/' : c);
            if (mark.note.empty()) out += "(no note)";
            out += " |\n";
            ++number;
        }
        out += "\nx and y are fractions of the frame from its top left corner.\n\n";
    }

    out += "## State\n\n";
    for (const auto& field : report.context) out += "- " + field.first + ": " + field.second + "\n";
    if (!report.reproduce.empty()) out += "\n## Reproduce\n\n```sh\n" + report.reproduce + "\n```\n";
    return out;
}

std::string feedback_folder_name(int number, const std::string& summary) {
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04d", number);
    std::string name = prefix;
    std::string slug;
    bool gap = false;
    for (char c : summary) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) && u < 128) {
            if (gap && !slug.empty()) slug += '-';
            gap = false;
            slug += char(std::tolower(u));
        } else {
            gap = true;
        }
        if (slug.size() >= 40) break;
    }
    if (!slug.empty()) name += "-" + slug;
    return name;
}

}  // namespace game
