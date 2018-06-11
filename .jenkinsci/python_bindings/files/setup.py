
from setuptools import setup, Distribution, find_packages

class BinaryDistribution(Distribution):
      def has_ext_modules(self):
            return True

setup(name='iroha',
      version="0.0.1",
      description='Python library for Hyperledger Iroha',
      url='http://github.com/hyperledger/iroha',
      author='Soramitsu Co Ltd',
      author_email='ci@soramitsu.co.jp',
      license='MIT',
      packages=find_packages(exclude=['dist','build','*.pyc', '.DS_Store', '.vscode', '__pycache__']),
      package_data={
            'iroha': ['_iroha.exp', '_iroha.lib', '_iroha.pyd', '_iroha.pyd.manifest', 'bindings.lib', 'libprotobuf.dll', '_iroha.so']
      },
         classifiers=[
            'License :: OSI Approved :: Apache Software License',
            'Development Status :: 4 - Beta',
            'Programming Language :: Python',
            'Operating System :: OS Independent',
            'Programming Language :: Python :: 2.7',
            'Programming Language :: Python :: 3.5'
      ],
      install_requires=[
            'protobuf=>3.5.2.post1',
            'grpcio=>1.12.1'
      ],
      zip_safe=False,
      distclass=BinaryDistribution
)
