pipeline {
  environment {}

  options {
    buildDiscarder(logRotator(numToKeepStr: '20'))
    timeout(time: 3, unit: 'HOURS')
    timestamps()
  }

  agent any
  stages {
  	stage('first') {
  		agent { label 'master' }
  		steps {
  			script {
  				sh "env"
  			}
  		}
  	}
  }
}