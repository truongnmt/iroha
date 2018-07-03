pipeline {

  options {
    buildDiscarder(logRotator(numToKeepStr: '20'))
    timeout(time: 3, unit: 'HOURS')
    timestamps()
  }

  agent any
  stages {
  	stage('first') {
  		agent { label 'aws_build' }
  		steps {
  			script {
  				sh "env"
  				sh "sudo mkdir -p /var/j2 && sudo chown -R ubuntu.ubuntu /var/j2"
  				ws('/var/j2') {
  				    sh "echo hi > file"
  				}
  			}
  		}
  	}
  	stage('second') {
  	    agent { label 'aws_build' }
  	    steps {
  	        script {
  	            ws('/var/j2') {
  	                sh "cat file"
  	            }
  	        }
  	    }
  	}
  }
}