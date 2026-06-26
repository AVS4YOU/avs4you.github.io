#ifndef UIFILESYSTEM_H
#define UIFILESYSTEM_H

#include "uidefines.h"


namespace UIFileSystem
{
DECL_VISUALUI bool fileExists(const tstring &filePath);
DECL_VISUALUI bool dirExists(const tstring &dirName);
DECL_VISUALUI bool dirIsEmpty(const tstring &dirName);

// Ensures that all intermediate directories in the given `path`
// (starting from `root_offset`) exist, creating them as necessary.
DECL_VISUALUI bool makePath(const tstring &path, size_t root_offset = 3);

DECL_VISUALUI bool copyFile(const tstring &oldFilePath, const tstring &newFilePath);
DECL_VISUALUI bool replaceFile(const tstring &oldFilePath, const tstring &newFilePath);
DECL_VISUALUI bool replaceDir(const tstring &sourceDir, const tstring &destDir);
DECL_VISUALUI bool removeFile(const tstring &filePath);
DECL_VISUALUI bool removeDirRecursively(const tstring &dir);
DECL_VISUALUI tstring fromNativeSeparators(const tstring &path);
DECL_VISUALUI tstring toNativeSeparators(const tstring &path);
DECL_VISUALUI tstring parentPath(const tstring &path);
DECL_VISUALUI tstring tempPath();
DECL_VISUALUI tstring appPath();
};

#endif // UIFILESYSTEM_H
