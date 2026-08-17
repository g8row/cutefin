# src/ui is on the include path in its own right so widgets can include each
# other as "widgets/Foo.h" while still reaching the library headers as
# "core/Foo.h" / "api/Foo.h" through cutefin_core's PUBLIC ${CMAKE_SOURCE_DIR}/src.
target_include_directories(cutefin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(cutefin PRIVATE
    MainWindow.h
    MainWindow.cpp
    ServerSelectPage.h
    ServerSelectPage.cpp
    LoginPage.h
    LoginPage.cpp
    HomePage.h
    HomePage.cpp
    DetailPage.h
    DetailPage.cpp
    LibraryPage.h
    LibraryPage.cpp
    widgets/SmoothScrollArea.h
    widgets/SmoothScrollArea.cpp
    widgets/AsyncImage.h
    widgets/AsyncImage.cpp
    widgets/PosterCard.h
    widgets/PosterCard.cpp
    widgets/MediaRow.h
    widgets/MediaRow.cpp
    widgets/PosterGrid.h
    widgets/PosterGrid.cpp
)
