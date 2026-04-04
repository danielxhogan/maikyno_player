#pragma once

#ifdef __unix__
#define HAVE_UNIX
#endif

#ifdef _WIN32
#define HAVE_WIN32
#endif

#ifdef __APPLE__
#define HAVE_APPLE
#endif
