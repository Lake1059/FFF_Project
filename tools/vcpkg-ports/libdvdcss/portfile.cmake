vcpkg_download_distfile(ARCHIVE
    URLS "https://download.videolan.org/pub/videolan/libdvdcss/${VERSION}/libdvdcss-${VERSION}.tar.xz"
    FILENAME "libdvdcss-${VERSION}.tar.xz"
    SHA512 edd8b00af2621d1b8fb7eac818e9e7a763aabb1b2868d2fc56836b8a22d6d6bc1855b027125557263a6607c86e5d32214b86ab4e22ecb6ca51964abd22574ea0
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
vcpkg_configure_meson(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_install_meson()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
