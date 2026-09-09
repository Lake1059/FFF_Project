vcpkg_download_distfile(ARCHIVE
    URLS "https://download.videolan.org/pub/videolan/libbluray/${VERSION}/libbluray-${VERSION}.tar.xz"
    FILENAME "libbluray-${VERSION}.tar.xz"
    SHA512 76d686260b7cceb9e9e0272e4b9c4a815511925240dc4b69107c0816131728912f5cf3d08d5eab769024e024377f6591d1bbd51a459039a521639d569473cec6
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Denable_tools=false
        -Dbdj_jar=disabled
        -Dfreetype=enabled
        -Dlibxml2=enabled
        -Dfontconfig=disabled
)
vcpkg_install_meson()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
