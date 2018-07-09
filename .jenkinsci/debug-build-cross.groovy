#!/usr/bin/env groovy

def buildBody(buildType, coverage, workspace) {
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
        -DCMAKE_BUILD_TYPE=${buildType} \
        -DCOVERAGE=${(coverage) ? 'ON' : 'OFF'} \
        -DTESTING=${(buildType == 'Debug') ? 'ON' : 'OFF'} \
        -DCMAKE_TOOLCHAIN_FILE=/opt/toolchain.cmake
    """
    sh "cmake --build build -- -j${env.PARALLELISM}"
    if(coverage) {
      sh "cmake --build build --target coverage.init.info"
    }
    sh "ccache --show-stats"
    sh "mkdir -p ${workspace}/build/shared_libs"
    sh "cp -r \$STAGING/lib/* ${workspace}/build/shared_libs"
}

def doDebugBuild(buildType, coverage, workspace, dockerImage) {
  if (dockerImage) {
    //def dPullOrBuild = load ".jenkinsci/docker-pull-or-build.groovy"
    //def previousCommit = pCommit.previousCommitOrCurrent()
    // def iC = dPullOrBuild.dockerPullOrUpdate(
    //     dockerImage,
    //     // TODO: fix paths
    //     "${env.GIT_RAW_BASE_URL}/${env.GIT_COMMIT}/docker/develop/Dockerfile",
    //     "${env.GIT_RAW_BASE_URL}/${previousCommit}/docker/develop/Dockerfile",
    //     "${env.GIT_RAW_BASE_URL}/develop/docker/develop/Dockerfile",
    //     ['PARALLELISM': env.PARALLELISM])
    // if(GIT_LOCAL_BRANCH == 'develop') {
    //   withRegistry('https://registry.hub.docker.com', env.DOCKER_REGISTRY_CREDENTIALS_ID) {
    //     iC.push
    //   }
    // }
    // iC.inside("-v /opt/ccache:${CCACHE_DIR}") {
    docker.image(dockerImage).inside("-v /opt/ccache:${CCACHE_DIR}") {
      buildBody(buildType, coverage, workspace)
    }
    //}
  }
  else {
    buildBody(buildType, coverage, workspace)
  }
}


return this
