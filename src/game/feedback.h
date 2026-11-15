// The in-game feedback report (docs/SHOWCASE.md, "The feedback tool"): the
// playtester pauses with F2, clicks the frame to mark what is wrong, types a
// note per mark, and saves. A report is a folder with the frame as shown, the
// same frame with the marks drawn on it, and report.md: the notes, where each
// mark is, and enough of the game's state to get back to the moment.
//
// This file is the report's data and its text. It knows nothing of the
// window or the renderer, so the format has a test (tests/test_feedback.cpp).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace game {

// One mark on the frame. x and y are fractions of the frame, 0,0 top left,
// so a mark means the same place at any window size.
struct FeedbackMark {
    float x = 0.0f;
    float y = 0.0f;
    std::string note;
};

struct FeedbackReport {
    std::string summary;                 // the note for the whole frame
    std::vector<FeedbackMark> marks;     // numbered from 1 in the order clicked
    // Name and value, in the order given: the model, the scenario, the
    // camera, the flight state. The app fills it; the report prints it.
    std::vector<std::pair<std::string, std::string>> context;
    std::string reproduce;               // a command line that returns to the moment
    std::string frame_file = "frame.bmp";    // the frame, nothing drawn on it
    std::string marked_file = "marked.bmp";  // the same frame with the marks
    std::string when;                    // local time, "2026-11-14 22:10:05"
};

// "top left", "centre", "bottom right": a mark's ninth of the frame, for a
// reader who has the text and not the picture.
const char* feedback_region(float x, float y);

// The report as Markdown.
std::string feedback_markdown(const FeedbackReport& report);

// "0007-legs-loose-on-attack": the folder's name from its number and the
// summary's first words. Lower case, letters and digits, at most 40 letters.
std::string feedback_folder_name(int number, const std::string& summary);

}  // namespace game
