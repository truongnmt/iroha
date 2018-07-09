#!/usr/bin/env groovy

def doDebugTest(workspace) {
	try {
	  sh "docker network create ${env.IROHA_NETWORK}"
	  docker.image('postgres:9.5').withRun(""
	  + " -e POSTGRES_USER=${env.IROHA_POSTGRES_USER}"
	  + " -e POSTGRES_PASSWORD=${env.IROHA_POSTGRES_PASSWORD}"
	  + " --name ${env.IROHA_POSTGRES_HOST}"
	  + " --network=${env.IROHA_NETWORK}") {
	    docker.image('local/linux-test-env').inside(""
		+ " -e IROHA_POSTGRES_HOST=${env.IROHA_POSTGRES_HOST}"
		+ " -e IROHA_POSTGRES_PORT=${env.IROHA_POSTGRES_PORT}"
		+ " -e IROHA_POSTGRES_USER=${env.IROHA_POSTGRES_USER}"
		//+ " -e IROHA_POSTGRES_PASSWORD=${env.IROHA_POSTGRES_PASSWORD}"
		+ " -e IROHA_POSTGRES_PASSWORD=123"
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

return this
