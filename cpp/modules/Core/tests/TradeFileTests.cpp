#include "Core/Data/TradeFile.hpp"

#include <doctest/doctest.h>

#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view Sample = "timestamp,side,price,amount\n1,buy,1,1\n2,sell,2,2\n";

/** A folder for one test's files, removed afterwards. */
class TemporaryFolder
{
public:
    TemporaryFolder()
        : mPath(fs::temp_directory_path() / "tradeviewer-tradefile-tests")
    {
        fs::remove_all(mPath);
        fs::create_directories(mPath);
    }

    ~TemporaryFolder() { fs::remove_all(mPath); }

    [[nodiscard]] fs::path operator/(std::string_view name) const { return mPath / name; }

private:
    fs::path mPath;
};

void writePlain(const fs::path& path, std::string_view text)
{
    std::ofstream(path, std::ios::binary) << text;
}

void writeGzip(const fs::path& path, std::string_view text)
{
    const gzFile file = gzopen(path.c_str(), "wb");
    CHECK(file != nullptr);
    if (!file) {
        return;
    }
    gzwrite(file, text.data(), static_cast<unsigned>(text.size()));
    gzclose(file);
}

} // namespace

TEST_CASE("Trade files are read compressed or not, whatever they're called")
{
    const TemporaryFolder folder;
    writePlain(folder / "plain.csv", Sample);
    writeGzip(folder / "packed.csv.gz", Sample);
    writeGzip(folder / "packed-but-named.csv", Sample);
    writePlain(folder / "plain-but-named.csv.gz", Sample);
    for (const auto* name : { "plain.csv", "packed.csv.gz", "packed-but-named.csv", "plain-but-named.csv.gz" }) {
        CAPTURE(name);
        const auto trades = Core::Data::readTradeFile(folder / name);
        CHECK(trades);
        if (trades) {
            CHECK(trades->size() == 2);
        }
    }
}

TEST_CASE("A trade file that can't be read says so, and which")
{
    const TemporaryFolder folder;
    const auto errorOf = [](const fs::path& path) {
        const auto trades = Core::Data::readTradeFile(path);
        CHECK_FALSE(trades);
        return trades ? std::string() : trades.error();
    };

    CHECK(errorOf(folder / "missing.csv").find("missing.csv") != std::string::npos);

    // A folder.
    CHECK(errorOf(folder / ".").find("Can't read") != std::string::npos);

    writePlain(folder / "empty.csv", "");
    CHECK(errorOf(folder / "empty.csv").ends_with("empty.csv: The file is empty"));

    // A gzip file cut off, as by a failed download: not just the trades before the cut.
    std::string many(std::string{ Sample });
    for (int i = 0; i < 100'000; ++i) {
        many += std::to_string(i) + ",buy,1,1\n";
    }
    writeGzip(folder / "cut.csv.gz", many);
    fs::resize_file(folder / "cut.csv.gz", fs::file_size(folder / "cut.csv.gz") / 2);
    CHECK(errorOf(folder / "cut.csv.gz").find("Can't read") != std::string::npos);

    // A gzip file damaged in the middle.
    writeGzip(folder / "damaged.csv.gz", many);
    {
        std::fstream file(folder / "damaged.csv.gz", std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(static_cast<std::streamoff>(fs::file_size(folder / "damaged.csv.gz") / 2));
        file << "not what it should be";
    }
    CHECK(errorOf(folder / "damaged.csv.gz").find("damaged.csv.gz") != std::string::npos);

    // The error from a line names the file too.
    writePlain(folder / "bad.csv", "timestamp,side,price,amount\n1,hold,1,1\n");
    CHECK(errorOf(folder / "bad.csv").find("bad.csv") != std::string::npos);
}
