
typedef void FileResultsCallback(
    const std::vector<std::u8string>& Files,
    const std::vector<std::u8string>& Directories
    );

bool
FindFiles(
    const std::string& Root,
    FileResultsCallback& Callback);