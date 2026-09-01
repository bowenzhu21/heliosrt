pipeline {
  agent { label 'cpu-linux' }
  stages {
    stage('Build and CPU correctness') {
      steps {
        sh 'make test'
        sh 'make run'
      }
    }
    stage('Sanitizers') {
      steps {
        sh 'make test-sanitize'
      }
    }
    stage('Benchmark contract') {
      steps {
        sh 'make validate'
        sh 'make fingerprint'
      }
    }
  }
  post {
    always {
      archiveArtifacts artifacts: 'artifacts/**/*', allowEmptyArchive: true
    }
  }
}
