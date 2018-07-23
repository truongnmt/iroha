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

def doDebugTest(workspace, dockerImage) {
  try {
    sh "docker network create ${env.IROHA_NETWORK}"
    docker.image('postgres:9.5').withRun(""
    + " -e POSTGRES_USER=${env.IROHA_POSTGRES_USER}"
    + " -e POSTGRES_PASSWORD=${env.IROHA_POSTGRES_PASSWORD}"
    + " --name ${env.IROHA_POSTGRES_HOST}"
    + " --network=${env.IROHA_NETWORK}") {
      docker.image(dockerImage).inside(""
    + " -e IROHA_POSTGRES_HOST=${env.IROHA_POSTGRES_HOST}"
    + " -e IROHA_POSTGRES_PORT=${env.IROHA_POSTGRES_PORT}"
    + " -e IROHA_POSTGRES_USER=${env.IROHA_POSTGRES_USER}"
    + " -e IROHA_POSTGRES_PASSWORD=${env.IROHA_POSTGRES_PASSWORD}"
    + " --network=${env.IROHA_NETWORK}") {
        sh "sudo cp -r ${workspace}/build/shared_libs/* /usr/lib/"
        sh "cd build; ctest --output-on-failure --no-compress-output -T Test || true"
        xunit testTimeMargin: '3000', thresholdMode: 2, thresholds: [failed(failureNewThreshold: '90', \
          failureThreshold: '50', unstableNewThreshold: '50', unstableThreshold: '20'), \
            skipped()], tools: [CTest(deleteOutputFiles: false, failIfNotNew: false, \
            pattern: 'build/Testing/**/Test.xml', skipNoTestFiles: false, stopProcessingIfError: true)]
    }
    }
  }
  catch(e) {
    sh("echo Something went wrong...")
  }
  finally {
      def currentResult = currentBuild.result ?: 'SUCCESS'
      sh("docker network rm ${env.IROHA_NETWORK}")
  }

}

def buildSteps(String label, String arch, String os, String buildType, Boolean coverage, environment, dockerImage) {
  return {
    node(label) {
      withEnv(environment) {
        // checkout to expose env vars
        def scmVars = checkout scm
        def workspace = "/var/jenkins/workspace/4c4825402c5cc2d4cb3217a9b62fe444499b2ca0-189-arm64-debian-stretch"
        //def workspace = "${env.WS_BASE_DIR}/${scmVars.GIT_COMMIT}-${env.BUILD_NUMBER}-${arch}-${os}"
        //sh("mkdir -p $workspace")
        dir(workspace) {
          // then checkout into actual workspace
          checkout scm
          doDebugBuild(buildType, coverage, workspace, dockerImage)
        }
      }
    }
  }
}

def testSteps(String label, String arch, String os, Boolean coverage, environment, dockerImage) {
  return {
    node(label) {
      withEnv(environment) {
        def scmVars = checkout scm
        //def workspace = "${env.WS_BASE_DIR}/${scmVars.GIT_COMMIT}-${env.BUILD_NUMBER}-${arch}-${os}"
        def workspace = "/var/jenkins/workspace/4c4825402c5cc2d4cb3217a9b62fe444499b2ca0-189-arm64-debian-stretch"
        dir(workspace) {
          doDebugTest(workspace, dockerImage)
        }
      }
    }
  }
}

return this
