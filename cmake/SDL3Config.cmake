# Used when SDL3 is built from source via add_subdirectory(extern/SDL).
# The parent project must add SDL before Pomme so that SDL3::SDL3 target exists.
# This allows find_package(SDL3) in Pomme to succeed.
if(NOT TARGET SDL3::SDL3)
	message(FATAL_ERROR "SDL3::SDL3 target not found. SDL must be added via add_subdirectory before find_package(SDL3).")
endif()
set(SDL3_FOUND TRUE)
