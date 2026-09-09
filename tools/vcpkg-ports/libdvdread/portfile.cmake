vcpkg_download_distfile(ARCHIVE
    URLS "https://download.videolan.org/pub/videolan/libdvdread/${VERSION}/libdvdread-${VERSION}.tar.bz2"
    FILENAME "libdvdread-${VERSION}.tar.bz2"
    SHA512 c98055829ccbbe2bb7b5df48f6e279cf6e91ecab39a35c37081581452f7df7531c1d7b8abd641c55eeee357235b2658b31478dbc307995cffdf8c011bd0dd0bc
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" PATCHES msvc.diff no-undefined.diff)
# MSVC allocates a full int for sub-byte bitfield groups even under pack(1).
# These IFO fields are byte-aligned on disc; use byte storage consistently.
file(READ "${SOURCE_PATH}/src/dvdread/ifo_types.h" ifo_types)
string(REPLACE "unsigned int" "unsigned char" ifo_types "${ifo_types}")
file(WRITE "${SOURCE_PATH}/src/dvdread/ifo_types.h" "${ifo_types}")
file(REMOVE_RECURSE "${SOURCE_PATH}/msvc/include/inttypes.h")
file(TOUCH "${SOURCE_PATH}/msvc/include/strings.h")
vcpkg_find_acquire_program(PKGCONFIG)
cmake_path(GET PKGCONFIG PARENT_PATH pkgconfig_dir)
vcpkg_add_to_path("${pkgconfig_dir}")
cmake_path(RELATIVE_PATH SOURCE_PATH BASE_DIRECTORY "${CURRENT_BUILDTREES_DIR}" OUTPUT_VARIABLE sources)
vcpkg_configure_make(SOURCE_PATH "${SOURCE_PATH}" AUTOCONFIG OPTIONS
    --disable-apidoc --with-libdvdcss
    "CPPFLAGS=\$CPPFLAGS -DPATH_MAX=2048 -DWIN32_LEAN_AND_MEAN -I../${sources}/msvc/include -D_CRT_SECURE_NO_WARNINGS"
)
vcpkg_install_make()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
