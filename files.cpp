#include "qsync.h"

#include <future>

using namespace std;
namespace fs = std::filesystem;

//
// Using this to guarantee a unique FileId for each file.
// Assuming you have fewer than 2^64 - 1 files.
// (Do you really need that many files?)
//
static uint64_t FileId = 0;
const auto FileChunkSize = 100;


bool
DoesFileNeedUpdate(
    const std::filesystem::path& Destination,
    const FileInfo::Reader& File)
{
    u8string_view PathView((char8_t*)File.getPath().cStr());
    auto FullPath = Destination / PathView;
    error_code Error;
    auto Exists = fs::exists(FullPath, Error);
    if (Error) {
        // Failed to detect if the file exists, don't try to write to it.
        return false;
    } else if (!Exists) {
        return true;
    }
    auto Status = fs::status(FullPath, Error);
    if (Error) {
        // Failed to query file status, don't try to write to it.
        return false;
    }

    switch (Status.type()) {
    case fs::file_type::regular:
        if (File.getType() != FileInfo::Type::FILE) {
            return false;
        }
        break;
    case fs::file_type::directory:
        if (File.getType() != FileInfo::Type::DIR) {
            return false;
        }
        break;
    case fs::file_type::symlink: {
        auto LinkPath = fs::read_symlink(FullPath, Error);
        if (Error) {
            // todo: print error
            return false;
        }
        auto LinkStatus = fs::status(LinkPath, Error);
        if (Error) {
            // todo: print error
            return false;
        }
        if (fs::is_directory(LinkStatus) && File.getType() != FileInfo::Type::DIRSYMLINK) {
            return false;
        }
        if (fs::is_regular_file(LinkStatus) && File.getType() != FileInfo::Type::FILESYMLINK) {
            return false;
        }
        break;
    }
    default:
        // Unsupported file type, don't try to write to it.
        return false;
    }

    auto ModifiedTime = fs::last_write_time(FullPath, Error);
    if (Error) {
        // Failed to detect the file write time, don't try to write to it.
        return false;
    } 
    chrono::utc_time<chrono::seconds> FileTime(chrono::seconds(File.getModifiedTime()));
    if (chrono::file_clock::from_utc(FileTime) > ModifiedTime) {
        return true;
    }

    if (File.getType() != FileInfo::Type::FILE) {
        // If a directory already exists, don't bother checking the size
        return false;
    }

    auto Size = fs::file_size(FullPath, Error);
    if (Error) {
        // Failed to read the file size, don't try to write to it.
        return false;
    }
    if (Size != File.getSize()) {
        return true;
    }
    return false;
}

template<typename F>
void
DirItemToFileInfo(
    F Callback,
    const fs::path& Root,
    const fs::directory_entry& DirItem,
    const FileInfo::Type Type,
    const fs::path LinkPath = "")
{
    capnp::MallocMessageBuilder Message;
    error_code Error;
    auto Builder = Message.initRoot<FileInfo>();
    Builder.setType(Type);
    auto FileSize = DirItem.file_size(Error);
    if (Error) {
        // Probably should just error out here
        return;
    }
    Builder.setSize(FileSize);
    auto FileTime = DirItem.last_write_time(Error);
    if (Error) {
        // Use the minimum time in case of error?
        FileTime = fs::file_time_type::min();
    }
    Builder.setModifiedTime(
        chrono::time_point_cast<chrono::seconds>(
            chrono::file_clock::to_utc(FileTime)).time_since_epoch().count());
    auto Path = DirItem.path().lexically_relative(Root).generic_u8string();
    Builder.setPath((const char*)Path.data());
    auto Id = ++FileId;
    Builder.setId(Id);
    if ((Type == FileInfo::Type::FILESYMLINK ||
        Type == FileInfo::Type::DIRSYMLINK) && LinkPath != "") {
        auto LinkPathStr = LinkPath.generic_u8string();
        Builder.setLinkPath(LinkPathStr);
    }

    SerializedFileInfo Data;
    Data.reserve(Message.sizeInWords() * sizeof(capnp::word));
    VectorStream Stream(Data);
    capnp::writePackedMessage(Stream, Message);
    Callback(Id, std::move(Data));
}

template<typename F>
tuple<vector<fs::path>, bool>
ProcessFolder(
    F Callback,
    const fs::path& Root,
    const fs::path& Parent)
{
    bool Success = true;
    error_code Error;
    vector<fs::path> Directories{};
    for (auto DirItem : fs::directory_iterator{Parent, fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, Error}) {
        if (Error) {
            // todo: print error
            Success = false;
            break;
        }
        auto ItemStatus = DirItem.status(Error);
        if (Error) {
            // todo: print error
            Success = false;
            continue;
        }
        if (fs::is_directory(ItemStatus)) {
            Directories.push_back(DirItem.path());
            DirItemToFileInfo(Callback, Root, DirItem, FileInfo::Type::DIR);
        } else if (fs::is_regular_file(ItemStatus)) {
            DirItemToFileInfo(Callback, Root, DirItem, FileInfo::Type::FILE);
        } else if (fs::is_symlink(ItemStatus)) {
            auto LinkPath = fs::read_symlink(DirItem.path(), Error);
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            auto LinkStatus = fs::status(LinkPath, Error);
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            if (fs::is_directory(LinkStatus)) {
                // Do we traverse symlink directories?
                DirItemToFileInfo(Callback, Root, DirItem, FileInfo::Type::DIRSYMLINK, LinkPath);
            } else if (fs::is_regular_file(LinkStatus)) {
                DirItemToFileInfo(Callback, Root, DirItem, FileInfo::Type::FILESYMLINK, LinkPath);
            }
        }
    }
    return make_tuple(std::move(Directories), Success);
}

bool
FindFiles(
    const string& Root,
    std::function<FileResultsCallback> Callback)
{
    error_code Error;
    fs::path RootPath{Root};
    if (!fs::exists(RootPath, Error) || !fs::is_directory(RootPath, Error)) {
        cerr << "Path doesn't exist or isn't a directory" << endl;
        return false;
    }
    if (Error) {
        return false;
    }
    fs::path CanonicalRoot = fs::canonical(RootPath, Error);
    if (Error) {
        cerr << "Could not make canonical path for " << RootPath << endl;
        return false;
    }
    auto LexicalRoot = !RootPath.has_stem() ? CanonicalRoot : CanonicalRoot.parent_path();

    if (RootPath.has_stem()) {
        DirItemToFileInfo(Callback, LexicalRoot, fs::directory_entry(CanonicalRoot), FileInfo::Type::DIR);
    }

    bool Result = true;
    deque<fs::path> UnexploredDirs{};
    UnexploredDirs.push_back(CanonicalRoot);

    while (!UnexploredDirs.empty()) {
        vector<fs::path> Directories;
        bool DirSuccess;
        auto CurrentDirectory = UnexploredDirs.front();
        UnexploredDirs.pop_front();
        std::tie(Directories, DirSuccess) = ProcessFolder(Callback, LexicalRoot, CurrentDirectory);

        UnexploredDirs.insert(UnexploredDirs.end(), Directories.begin(), Directories.end());
        if (!DirSuccess) {
            Result = DirSuccess;
        }
    }

    return Result;
}

bool
FindFilesParallel(
    const string& Root,
    std::function<FileResultsCallback> Callback)
{
    error_code Error;
    fs::path RootPath{Root};
    if (!fs::exists(RootPath, Error) || !fs::is_directory(RootPath, Error)) {
        cerr << "Path doesn't exist or isn't a directory" << endl;
        return false;
    }
    if (Error) {
        return false;
    }
    fs::path CanonicalRoot = fs::canonical(RootPath, Error);
    if (Error) {
        cerr << "Could not make canonical path for " << RootPath << endl;
        return false;
    }
    auto LexicalRoot = !RootPath.has_stem() ? CanonicalRoot : CanonicalRoot.parent_path();

    bool Success = true;
    vector<fs::path> UnexploredDirs{};
    UnexploredDirs.push_back(CanonicalRoot);

    while (!UnexploredDirs.empty()) {
        vector<future<tuple<vector<fs::path>, bool>>> ParallelResults;
        for (auto const& Dir : UnexploredDirs) {
            ParallelResults.push_back(
                async(launch::async, ProcessFolder<std::function<FileResultsCallback>>, Callback, LexicalRoot, Dir));
        }

        vector<fs::path> Directories{};
        for (auto& Result : ParallelResults) {
            vector<fs::path> ResultDirs;
            bool ResultSuccess;

            tie(ResultDirs, ResultSuccess) = Result.get();
                Directories.insert(Directories.end(), ResultDirs.begin(), ResultDirs.end());
            if (!ResultSuccess) {
                Success = ResultSuccess;
            }
        }
        UnexploredDirs.swap(Directories);
    }
    return Success;
}
