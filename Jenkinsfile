pipeline {
  agent { label 'cpu-linux' }
  stages {
    stage('Build and CPU correctness') {
      steps {
        sh 'make test'
        sh 'make run'
      }
    }
  }
  post {
    always {
      archiveArtifacts artifacts: 'artifacts/**/*', allowEmptyArchive: true
    }
  }
}

