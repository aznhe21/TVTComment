#pragma once
#include <functional>

bool ImportLogfile(LPCTSTR srcPath, const std::function<FILE *(unsigned int &)> &onChatTag);
bool ImportLogfile(LPCTSTR srcPath, LPCTSTR destPath, unsigned int tmNew);
