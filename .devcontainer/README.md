# This folder contains all the elements used for libdragon dev containers
They are used to facilitate building the Docker images.

The available pre-build image is:
* `ghcr.io/dragonminded/libdragon`: contains the toolchain elements to build a ROM. **Important**: this is the latest avaiable image built from the `dragonminded` `trunk` branch.

The dev container folder consists of:
* `Dockerfile.libdragon-toolchain` to use the pre-build container with all the elements to build libdragon based ROMs using the `latest` image.
* `./sources/Dockerfile.libdragon-toolchain` to build the container from source with all the elements to build libdragon based ROMs after adjusting the toolchain locally.
