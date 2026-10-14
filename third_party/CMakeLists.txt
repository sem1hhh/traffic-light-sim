# Tek kaynakli harici bagimliliklar (header-only).
# Her biri <paket>/paket.h yapisinda yer alir; include yolu third_party/ olur.
add_library(third_party INTERFACE)
target_include_directories(third_party INTERFACE ${CMAKE_SOURCE_DIR}/third_party)