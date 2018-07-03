pipeline {
	environment {
		EFS_WS_NAME = "fs-53325c9a.efs.eu-west-1.amazonaws.com"
		EFS_WS_DIR = /home/ubuntu/j2
	}
  options {
    buildDiscarder(logRotator(numToKeepStr: '20'))
    timeout(time: 3, unit: 'HOURS')
    timestamps()
  }

  agent any
  stages {
  	stage('first') {
  		agent { label 'aws_build_2' }
  		steps {
  			script {
  				sh "env"
  				sh """
  					sudo mkdir -p ${EFS_WS_DIR}
  					sudo mount -t nfs4 -o nfsvers=4.1,rsize=1048576,wsize=1048576,hard,timeo=600,retrans=2 ${EFS_WS_NAME}:/ ${EFS_WS_DIR}
  					sudo chown -R ubuntu.ubuntu ${EFS_WS_DIR}"""
  				ws(EFS_WS_DIR) {
  				    sh "echo hi > file"
  				}
  			}
  		}
  	}
  	stage('second') {
  	    agent { label 'aws_build_2' }
  	    steps {
  	        script {
  	        	sh "sudo mount -t nfs4 -o nfsvers=4.1,rsize=1048576,wsize=1048576,hard,timeo=600,retrans=2 ${EFS_WS_NAME}:/ ${EFS_WS_DIR}"
	            ws(EFS_WS_DIR) {
	                sh "cat file"
	            }
  	        }
  	    }
  	}
  }
}