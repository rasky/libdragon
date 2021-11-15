# This folder contains all the elements used for libdragon dev container

You'll find Docker files for libdragon. They are used to facilitate building the Docker images.

The available pre build images are:

* ghcr.io/dragonminded/libdragon: contains all elements to build any ROM. **Important**: the size of this container is large as it contains all elements to build the toolchain.
The dev container folder contains:

* `Dockerfile.Libdragon` to use the pre build container with all the elements to build libdragon based ROMs using the `latest` image.
* `./sources/Dockerfile.Libdragon` to use build the container from the source with all the elements to build libdragon based ROMs after adjusting the toolchain locally.
