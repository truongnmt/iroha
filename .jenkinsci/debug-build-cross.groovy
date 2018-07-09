#!/usr/bin/env groovy

def doReleaseBuild() {
  docker.image(${DOCKER_REGISTRY_BASENAME}:crossbuild-debian-stretch-arm64).inside(""
  	+ " -v /opt/efs-test/build:/opt/iroha_build"
  	+ " -v /opt/efs-test/build/ccache:${CCACHE_DIR}") {
    sh """
      ccache --version
      ccache --show-stats
      ccache --zero-stats
      ccache --max-size=20G
    """
    sh """
      cmake \
        -H. \
        -Bbuild \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCOVERAGE=OFF \
        -DTESTING=ON \
        -DCMAKE_TOOLCHAIN_FILE=/opt/toolchain.cmake
    """
    sh "cmake --build build -- -j8"
    sh "ccache --show-stats"
  }