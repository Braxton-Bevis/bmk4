#include <zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr std::array<std::uint8_t, 8> kUnsignedMagic = {'I', 'W', 'f', 'f', 'u', '1', '0', '0'};
constexpr std::uint32_t kExpectedVersion = 5;
constexpr std::size_t kOuterHeaderSize = 12;
constexpr std::size_t kXFileSize = 44;
constexpr std::size_t kXAssetListSize = 16;
constexpr std::size_t kMinimumPayloadSize = kXFileSize + kXAssetListSize;
constexpr std::size_t kInflateChunkSize = 64 * 1024;
constexpr std::size_t kMaximumPayloadSize = 2ull * 1024 * 1024 * 1024;

constexpr std::array<std::string_view, 9> kBlockNames = {
    "temp",
    "runtime",
    "large_runtime",
    "physical_runtime",
    "virtual",
    "large",
    "physical",
    "vertex",
    "index",
};

struct Arguments
{
    fs::path input;
    fs::path output;
    std::optional<fs::path> fixtureAllowlistRoot;
};

struct XFileFields
{
    std::uint32_t size;
    std::uint32_t externalSize;
    std::array<std::uint32_t, 9> blockSizes;
};

struct XAssetListFields
{
    std::uint32_t scriptStringCount;
    std::uint32_t scriptStringsToken;
    std::uint32_t assetCount;
    std::uint32_t assetsToken;
};

class OracleError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class AllowlistError final : public OracleError
{
public:
    using OracleError::OracleError;
};

void PrintUsage()
{
    std::cerr << "usage: bmk4-ff-oracle --input <zone.ff> --output <dump.txt> "
                 "[--fixture-allowlist-root <repo-root>]\n";
}

Arguments ParseArguments(const int argc, char** argv)
{
    Arguments args;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option(argv[index]);
        if (index + 1 >= argc)
            throw OracleError("missing value for option " + std::string(option));

        const fs::path value(argv[++index]);
        if (option == "--input")
        {
            if (!args.input.empty())
                throw OracleError("--input specified more than once");
            args.input = value;
        }
        else if (option == "--output")
        {
            if (!args.output.empty())
                throw OracleError("--output specified more than once");
            args.output = value;
        }
        else if (option == "--fixture-allowlist-root")
        {
            if (args.fixtureAllowlistRoot.has_value())
                throw OracleError("--fixture-allowlist-root specified more than once");
            args.fixtureAllowlistRoot = value;
        }
        else
        {
            throw OracleError("unknown option " + std::string(option));
        }
    }

    if (args.input.empty() || args.output.empty())
        throw OracleError("--input and --output are required");
    return args;
}

bool IsWithinRoot(const fs::path& candidate, const fs::path& root)
{
    const fs::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return candidate == root;

    for (const auto& component : relative)
    {
        if (component == "..")
            return false;
    }
    return true;
}

fs::path CanonicalOutputPath(const fs::path& output)
{
    const fs::path parent = output.has_parent_path() ? output.parent_path() : fs::current_path();
    if (!fs::exists(parent) || !fs::is_directory(parent))
        throw OracleError("output parent directory does not exist: " + parent.string());

    if (fs::exists(output))
        return fs::canonical(output);
    return fs::canonical(parent) / output.filename();
}

void EnforceFixtureAllowlist(const Arguments& args)
{
    if (!args.fixtureAllowlistRoot.has_value())
        return;

    const fs::path root = fs::canonical(*args.fixtureAllowlistRoot);
    if (!fs::is_directory(root))
        throw OracleError("fixture allowlist root is not a directory: " + root.string());

    const fs::path input = fs::canonical(args.input);
    const fs::path output = CanonicalOutputPath(args.output);
    if (!IsWithinRoot(input, root))
        throw AllowlistError("fixture allowlist refused input outside repo root: " + input.string());
    if (!IsWithinRoot(output, root))
        throw AllowlistError("fixture allowlist refused output outside repo root: " + output.string());
}

std::vector<std::uint8_t> ReadFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw OracleError("cannot open input: " + path.string());

    const std::streamoff length = stream.tellg();
    if (length < 0)
        throw OracleError("cannot determine input length: " + path.string());
    if (static_cast<std::uint64_t>(length) > std::numeric_limits<uInt>::max())
        throw OracleError("input is too large for the IW3 zlib lane");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), length))
        throw OracleError("cannot read input: " + path.string());
    return bytes;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
        throw OracleError("truncated little-endian field at offset " + std::to_string(offset));
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<std::uint8_t> InflatePayload(const std::vector<std::uint8_t>& fileBytes)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(fileBytes.data() + kOuterHeaderSize);
    stream.avail_in = static_cast<uInt>(fileBytes.size() - kOuterHeaderSize);

    const int initResult = inflateInit(&stream);
    if (initResult != Z_OK)
        throw OracleError("zlib inflateInit failed with code " + std::to_string(initResult));

    std::vector<std::uint8_t> payload;
    int result = Z_OK;
    while (result != Z_STREAM_END)
    {
        if (payload.size() > kMaximumPayloadSize - kInflateChunkSize)
        {
            inflateEnd(&stream);
            throw OracleError("decompressed payload exceeds the 2 GiB safety limit");
        }

        const std::size_t oldSize = payload.size();
        payload.resize(oldSize + kInflateChunkSize);
        stream.next_out = payload.data() + oldSize;
        stream.avail_out = static_cast<uInt>(kInflateChunkSize);
        result = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = kInflateChunkSize - stream.avail_out;
        payload.resize(oldSize + produced);

        if (result != Z_OK && result != Z_STREAM_END)
        {
            inflateEnd(&stream);
            throw OracleError("zlib inflate failed with code " + std::to_string(result));
        }
        if (result == Z_OK && produced == 0 && stream.avail_in == 0)
        {
            inflateEnd(&stream);
            throw OracleError("truncated zlib stream");
        }
    }

    if (stream.avail_in != 0)
    {
        inflateEnd(&stream);
        throw OracleError("trailing bytes after the zlib stream");
    }
    inflateEnd(&stream);
    return payload;
}

std::uint64_t Fnv1a64(const std::uint8_t* bytes, const std::size_t size)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Hex32(const std::uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

std::string Hex64(const std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

XFileFields ParseXFile(const std::vector<std::uint8_t>& payload)
{
    XFileFields fields{};
    fields.size = ReadU32(payload, 0);
    fields.externalSize = ReadU32(payload, 4);
    for (std::size_t index = 0; index < fields.blockSizes.size(); ++index)
        fields.blockSizes[index] = ReadU32(payload, 8 + index * 4);
    return fields;
}

XAssetListFields ParseXAssetList(const std::vector<std::uint8_t>& payload)
{
    const std::size_t offset = kXFileSize;
    return {
        ReadU32(payload, offset),
        ReadU32(payload, offset + 4),
        ReadU32(payload, offset + 8),
        ReadU32(payload, offset + 12),
    };
}

std::string BuildReport(
    const std::vector<std::uint8_t>& fileBytes,
    const std::vector<std::uint8_t>& payload,
    const XFileFields& xfile,
    const XAssetListFields& assets)
{
    const bool emptyFixture = payload.size() == kMinimumPayloadSize
        && xfile.size == 0 && xfile.externalSize == 0
        && std::all_of(xfile.blockSizes.begin(), xfile.blockSizes.end(), [](const std::uint32_t size) { return size == 0; })
        && assets.scriptStringCount == 0 && assets.scriptStringsToken == 0
        && assets.assetCount == 0 && assets.assetsToken == 0;

    std::ostringstream report;
    report << "schema=bmk4.ff-oracle.v1\n";
    report << "container.magic=IWffu100\n";
    report << "container.version=" << kExpectedVersion << "\n";
    report << "container.secure=0\n";
    report << "container.input_bytes=" << fileBytes.size() << "\n";
    report << "container.compressed_bytes=" << fileBytes.size() - kOuterHeaderSize << "\n";
    report << "container.decompressed_bytes=" << payload.size() << "\n";
    report << "xfile.size=" << xfile.size << "\n";
    report << "xfile.external_size=" << xfile.externalSize << "\n";
    report << "blocks.count=" << xfile.blockSizes.size() << "\n";
    for (std::size_t index = 0; index < xfile.blockSizes.size(); ++index)
    {
        report << "block[" << index << "].name=" << kBlockNames[index] << "\n";
        report << "block[" << index << "].bytes=" << xfile.blockSizes[index] << "\n";
    }
    report << "assets.total=" << assets.assetCount << "\n";
    report << "assets.pointer_token=" << Hex32(assets.assetsToken) << "\n";
    report << "asset_type_counts.observed=" << (emptyFixture ? 1 : 0) << "\n";
    report << "asset_type_counts.count=0\n";
    report << "script_strings.count=" << assets.scriptStringCount << "\n";
    report << "script_strings.pointer_token=" << Hex32(assets.scriptStringsToken) << "\n";
    report << "script_strings.metadata_hash.fnv1a64="
           << Hex64(Fnv1a64(payload.data() + kXFileSize, 8)) << "\n";
    report << "delayed_records.observed=" << (emptyFixture ? 1 : 0) << "\n";
    report << "delayed_records.count=0\n";
    report << "delayed_records.bytes=0\n";
    report << "external_references.observed=" << (emptyFixture ? 1 : 0) << "\n";
    report << "external_references.declared_bytes=" << xfile.externalSize << "\n";
    report << "external_references.count=0\n";
    report << "targeted_hash.xfile.fnv1a64=" << Hex64(Fnv1a64(payload.data(), kXFileSize)) << "\n";
    report << "targeted_hash.asset_list.fnv1a64="
           << Hex64(Fnv1a64(payload.data() + kXFileSize, kXAssetListSize)) << "\n";
    report << "targeted_hash.decompressed_payload.fnv1a64=" << Hex64(Fnv1a64(payload.data(), payload.size())) << "\n";
    report << "runtime_observation.complete=" << (emptyFixture ? 1 : 0) << "\n";
    return report.str();
}

void WriteReport(const fs::path& output, const std::string& report)
{
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream)
        throw OracleError("cannot open output: " + output.string());
    stream.write(report.data(), static_cast<std::streamsize>(report.size()));
    if (!stream)
        throw OracleError("cannot write output: " + output.string());
}
} // namespace

int main(const int argc, char** argv)
{
    try
    {
        const Arguments args = ParseArguments(argc, argv);
        if (!fs::exists(args.input) || !fs::is_regular_file(args.input))
            throw OracleError("input is not a regular file: " + args.input.string());

        EnforceFixtureAllowlist(args);
        const std::vector<std::uint8_t> fileBytes = ReadFile(args.input);
        if (fileBytes.size() < kOuterHeaderSize + 1)
            throw OracleError("fastfile is shorter than the IW3 header plus zlib payload");
        if (!std::equal(kUnsignedMagic.begin(), kUnsignedMagic.end(), fileBytes.begin()))
            throw OracleError("only unsigned IW3 fastfiles with magic IWffu100 are accepted");
        if (ReadU32(fileBytes, 8) != kExpectedVersion)
            throw OracleError("fastfile version is not IW3 version 5");

        const std::vector<std::uint8_t> payload = InflatePayload(fileBytes);
        if (payload.size() < kMinimumPayloadSize)
            throw OracleError("decompressed payload is too short for XFile and XAssetList");

        const XFileFields xfile = ParseXFile(payload);
        const XAssetListFields assets = ParseXAssetList(payload);
        WriteReport(args.output, BuildReport(fileBytes, payload, xfile, assets));
        return 0;
    }
    catch (const AllowlistError& error)
    {
        std::cerr << "ff-oracle: " << error.what() << '\n';
        return 3;
    }
    catch (const OracleError& error)
    {
        std::cerr << "ff-oracle: " << error.what() << '\n';
        PrintUsage();
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ff-oracle: unexpected error: " << error.what() << '\n';
        return 2;
    }
}
