Installation/Usage
==================
The library uses CMake and provides corresponding targets, allowing for easy integration using CMake's FetchContent feature.
A separate target is provided for each of the components: low-level API, middleware API, and software NAA.

The use in a minimal project would look as follows:

.. code-block:: cmake
   :caption: Minimaler FetchContent-Einbau

   cmake_minimum_required(VERSION 3.21)
   project(example-project C)

   include(FetchContent)

   FetchContent_Declare(naaice
     GIT_REPOSITORY https://git.gfz.de/naaice/naa-communication-prototype.git
     GIT_TAG main
   )

   FetchContent_MakeAvailable(naaice)

   add_executable(test test.c)
   target_link_libraries(test PRIVATE naaice::middleware)
