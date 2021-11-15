# This folder contains all the elements used for libdragon dev container

You'll find Docker files for libdragon. They are used to facilitate building the Docker images.

The available pre build images are:

* ghcr.io/dragonminded/dev-container-all: contains all elements to build any ROM. **Important**: the size of this container is large as it contains pre-built libraries and example ROM's. If you are interested in only building from the toolchain, you are better off using the following dedicated image:
* ghcr.io/dragonminded/dev-container-toolchain: contains all elements to build the toolchain
 
To choose the dev container you want to use, adjust `devcontainer.json` and change the `"dockerFile": "Dockerfile.<TYPE>"` element for the image you'd liked to use:

* `Dockerfile.All` to use the pre build container with all the elements to build all the ROMs using the latest `stable` image.
* `Dockerfile.Toolchain` to use the pre build container with all the elements to build libdragon based ROMs using the latest `stable` image.
* `./sources/Dockerfile.All` to use build the container from the source with all the elements to build libdragon and all the example ROMs
* `./sources/Dockerfile.Toolchain` to use build the container from the source with all the elements to build libdragon based ROMs
