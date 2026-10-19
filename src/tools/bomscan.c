/*
 * bomscan.c - UTF-8 BOM scanner for the build.
 *
 * Usage: bomscan <dir-or-file> [...]
 *
 * Recursively scans directories for *.c / *.h files and fails (exit 1)
 * if any file starts with the UTF-8 BOM (EF BB BF). Batch findstr cannot
 * reliably match BOM bytes, so the build compiles this tiny tool instead.
 * Plain Win32 console application, C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <windows.h>

static int files_with_bom = 0;

static int ends_with_ci( const char *name, const char *ext )
{
    size_t nlen = strlen( name );
    size_t elen = strlen( ext );

    if( nlen < elen ) {
        return( 0 );
    }
    name += nlen - elen;
    while( *ext != '\0' ) {
        if( tolower( (unsigned char)*name ) != tolower( (unsigned char)*ext ) ) {
            return( 0 );
        }
        name++;
        ext++;
    }
    return( 1 );
}

static void check_file( const char *path )
{
    FILE       *fp;
    unsigned char buf[3];

    fp = fopen( path, "rb" );
    if( fp == NULL ) {
        printf( "bomscan: warning: cannot open %s\n", path );
        return;
    }
    if( fread( buf, 1, 3, fp ) == 3
        && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF ) {
        printf( "bomscan: error: UTF-8 BOM found: %s\n", path );
        files_with_bom++;
    }
    fclose( fp );
}

static void scan_dir( const char *dir )
{
    char             pattern[_MAX_PATH];
    char             child[_MAX_PATH];
    WIN32_FIND_DATA  fd;
    HANDLE           h;

    sprintf( pattern, "%s\\*", dir );
    h = FindFirstFile( pattern, &fd );
    if( h == INVALID_HANDLE_VALUE ) {
        printf( "bomscan: warning: cannot read directory %s\n", dir );
        return;
    }
    do {
        if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            if( strcmp( fd.cFileName, "." ) == 0 ) continue;
            if( strcmp( fd.cFileName, ".." ) == 0 ) continue;
            sprintf( child, "%s\\%s", dir, fd.cFileName );
            scan_dir( child );
        } else if( ends_with_ci( fd.cFileName, ".c" )
                   || ends_with_ci( fd.cFileName, ".h" ) ) {
            sprintf( child, "%s\\%s", dir, fd.cFileName );
            check_file( child );
        }
    } while( FindNextFile( h, &fd ) );
    FindClose( h );
}

int main( int argc, char **argv )
{
    DWORD   attr;
    int     i;

    if( argc < 2 ) {
        printf( "usage: bomscan <dir-or-file> [...]\n" );
        return( 2 );
    }
    for( i = 1; i < argc; i++ ) {
        attr = GetFileAttributes( argv[i] );
        if( attr == 0xFFFFFFFF ) {
            printf( "bomscan: error: path not found: %s\n", argv[i] );
            files_with_bom++;
        } else if( attr & FILE_ATTRIBUTE_DIRECTORY ) {
            scan_dir( argv[i] );
        } else {
            check_file( argv[i] );
        }
    }
    if( files_with_bom == 0 ) {
        printf( "bomscan: OK, no UTF-8 BOM found.\n" );
        return( 0 );
    }
    printf( "bomscan: %d file(s) with UTF-8 BOM.\n", files_with_bom );
    return( 1 );
}
