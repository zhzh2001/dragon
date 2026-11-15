// The feedback report's text (game/feedback.h): what the playtester's marks
// and notes become on disk, which is what a later session reads.
#include <cstdio>
#include <string>

#include "game/feedback.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_failures;
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void test_regions() {
    std::printf("a mark's ninth of the frame\n");
    CHECK(std::string(game::feedback_region(0.1f, 0.1f)) == "top left");
    CHECK(std::string(game::feedback_region(0.5f, 0.5f)) == "centre");
    CHECK(std::string(game::feedback_region(0.9f, 0.9f)) == "bottom right");
    CHECK(std::string(game::feedback_region(0.5f, 0.9f)) == "bottom centre");
}

void test_markdown() {
    std::printf("the report names every mark, its place and its note\n");
    game::FeedbackReport report;
    report.summary = "Legs swing loose in the bite";
    report.when = "2026-11-14 22:10:05";
    game::FeedbackMark first;
    first.x = 0.42f;
    first.y = 0.61f;
    first.note = "hind leg | swings\npast the tail";
    report.marks.push_back(first);
    report.marks.push_back(game::FeedbackMark{});
    report.context.push_back({"model", "assets/embercrest-hero.glb"});
    report.context.push_back({"studio", "8 attack, t 7.25 s"});
    report.reproduce = "./build/dragon --model assets/embercrest-hero.glb --studio 8";
    const std::string text = game::feedback_markdown(report);
    CHECK(has(text, "# Feedback, 2026-11-14 22:10:05"));
    CHECK(has(text, "Legs swing loose in the bite"));
    CHECK(has(text, "| 1 | centre | 0.420 | 0.610 | hind leg / swings past the tail |"));
    CHECK(has(text, "| 2 | top left | 0.000 | 0.000 | (no note) |"));
    CHECK(has(text, "- model: assets/embercrest-hero.glb"));
    CHECK(has(text, "- studio: 8 attack, t 7.25 s"));
    CHECK(has(text, "./build/dragon --model assets/embercrest-hero.glb --studio 8"));
    CHECK(has(text, "(marked.bmp)"));
    CHECK(has(text, "`frame.bmp`"));

    std::printf("a report with no marks says so\n");
    game::FeedbackReport bare;
    const std::string empty = game::feedback_markdown(bare);
    CHECK(has(empty, "(no summary)"));
    CHECK(has(empty, "None. The note is about the whole frame."));
    CHECK(!has(empty, "## Reproduce"));
}

void test_folder_name() {
    std::printf("the folder is named for its number and its summary\n");
    CHECK(game::feedback_folder_name(7, "Legs loose on attack!") == "0007-legs-loose-on-attack");
    CHECK(game::feedback_folder_name(12, "") == "0012");
    CHECK(game::feedback_folder_name(3, "  --  ") == "0003");
    const std::string longer = game::feedback_folder_name(1, std::string(200, 'a'));
    CHECK(longer.size() == 5 + 40);
    CHECK(game::feedback_folder_name(2, "a/b\\c..d") == "0002-a-b-c-d");
}

}  // namespace

int main() {
    test_regions();
    test_markdown();
    test_folder_name();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
