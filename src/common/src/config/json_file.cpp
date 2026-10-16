#include "common/config/json_file.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace aion2::config {

std::optional<JsonValue> read_json_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return JsonValue::parse(text);
}

std::string write_file_atomic(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return "cannot create folder: " + ec.message();
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot open temp file";
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return "cannot write temp file";
        }
    }

    // rename replaces an existing target on both POSIX and Windows (MoveFileExW
    // with MOVEFILE_REPLACE_EXISTING under the hood).
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        const std::string why = "cannot replace file: " + ec.message();
        std::filesystem::remove(tmp, ec);
        return why;
    }
    return {};
}

std::string write_json_atomic(const std::filesystem::path& path, const JsonValue& doc) {
    return write_file_atomic(path, doc.stringify());
}

} // namespace aion2::config
