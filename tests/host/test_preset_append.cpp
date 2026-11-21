#include "test_framework.hpp"
#include "ctagSPDataModel.hpp"
#include "ctagResources.hpp"
#include <climits>
#include <filesystem>
#include <fstream>

std::string CTAG::RESOURCES::spiffsRoot;

TEST(preset_append_returns_the_real_index_and_preserves_existing_patches) {
    char directory[] = "/tmp/opencode/preset-test-XXXXXX";
    CHECK(mkdtemp(directory) != nullptr);
    const std::string previousRoot = CTAG::RESOURCES::spiffsRoot;
    CTAG::RESOURCES::spiffsRoot = directory;
    const std::filesystem::path root(directory);
    std::filesystem::create_directories(root / "data/sp");
    std::ofstream(root / "data/sp/mui-Test.jsn") <<
        R"({"params":[{"id":"tune","type":"int","min":0,"max":100}]})";
    std::ofstream patches(root / "data/sp/mp-Test.jsn");
    patches << R"({"activePatch":0,"patches":[)";
    for (int i = 0; i < 75; ++i)
        patches << (i ? "," : "") << "{\"name\":\"Patch " << i <<
            "\",\"params\":[{\"id\":\"tune\",\"current\":" << i << "}]}";
    patches << "]}";
    patches.close();
    {
        CTAG::SP::ctagSPDataModel model("Test", false);
        model.SetParamValue("tune", "current", 99);
        CHECK_EQ(model.SavePreset("Appended", INT_MAX), 75);
        rapidjson::Document listing;
        listing.Parse(model.GetCStrJSONPresets());
        CHECK_EQ(listing["activePresetNumber"].GetInt(), 75);
        CHECK_EQ(listing["presets"].Size(), 76u);
        CHECK_STREQ(listing["presets"][64]["name"].GetString(), "Patch 64");
        CHECK_STREQ(listing["presets"][75]["name"].GetString(), "Appended");
        CHECK_EQ(model.SavePreset("Overwrite", 5), 5);
    }
    CTAG::RESOURCES::spiffsRoot = previousRoot;
    std::filesystem::remove_all(root);
}
