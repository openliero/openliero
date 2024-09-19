# What the heck is overlay-triplets?

I'm glad you asked. Let thee learn the reasoning.

It all starts with [`CMAKE_OSX_DEPLOYMENT_TARGET`][0], which specifies the
minimum version of macOS that the binaries will run on.

_Okay, but what does that have to do with overlay-triplets?_

Even when this variable is set, vcpkg still compiles ports against the host's
OS SDK, resulting in linker warnings like this being spat out:

```sh
File.cpp.o was built for newer 'macOS' version (14) than being linked (12)

```

_That's whack, dude!_

Tell me about it! [This][1] particular question asks how to prevent this from
occurring, despite the existence of [`VCPKG_OSX_DEPLOYMENT_TARGET`][2].

**tl;dr** Create your own overlay-triplets and set `VCPKG_OSX_DEPLOYMENT_TARGET`
so that your ports can be linked against older macOS SDKs.

[0]: https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_DEPLOYMENT_TARGET.html
[1]: https://github.com/microsoft/vcpkg/discussions/39966
[2]: https://learn.microsoft.com/en-us/vcpkg/users/triplets#vcpkg_osx_deployment_target
