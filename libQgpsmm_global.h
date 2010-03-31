#ifndef LIBQGPSMM_GLOBAL_H
#define LIBQGPSMM_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(LIBQGPSMM_LIBRARY)
#  define LIBQGPSMMSHARED_EXPORT Q_DECL_EXPORT
#else
#  define LIBQGPSMMSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // LIBQGPSMM_GLOBAL_H
