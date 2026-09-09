vcpkg_download_distfile(ARCHIVE
    URLS "https://download.videolan.org/pub/videolan/libdvdnav/${VERSION}/libdvdnav-${VERSION}.tar.bz2"
    FILENAME "libdvdnav-${VERSION}.tar.bz2"
    SHA512 889bc987113189bfdfc1ebf603b256e5b228be37196455dcfdc7f66debe18e7298b82b807e86ff18a8925e491395ed4007c87cf18ba1ff1ccc213d86f5342a10
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" PATCHES msvc.diff no-undefined.diff)
file(REMOVE_RECURSE "${SOURCE_PATH}/msvc/include/inttypes.h")
file(MAKE_DIRECTORY "${SOURCE_PATH}/msvc/include/sys")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/time.h" DESTINATION "${SOURCE_PATH}/msvc/include/sys")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/unistd.h" DESTINATION "${SOURCE_PATH}/msvc/include")
vcpkg_find_acquire_program(PKGCONFIG)
cmake_path(GET PKGCONFIG PARENT_PATH pkgconfig_dir)
vcpkg_add_to_path("${pkgconfig_dir}")
cmake_path(RELATIVE_PATH SOURCE_PATH BASE_DIRECTORY "${CURRENT_BUILDTREES_DIR}" OUTPUT_VARIABLE sources)
vcpkg_make_configure(SOURCE_PATH "${SOURCE_PATH}" AUTORECONF OPTIONS
    "CPPFLAGS=\$CPPFLAGS -DPATH_MAX=2048 -DWIN32_LEAN_AND_MEAN -I../${sources}/msvc/include -D_CRT_SECURE_NO_WARNINGS"
)
vcpkg_make_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
