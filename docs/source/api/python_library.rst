Python library
==============


About Nexus
---------------
https://nexus.soramitsu.co.jp/ - our new PyPI, Maven, artifacts server with precompilled librares.
You can download binaries without compilation on the client side. It helps to save you time and disk space for installing dependencies like libboost, libz, etc.

Now our packages are distributed only for ``Windows x86_64`` and ``Linux x86_64`` with ``Python 2.7.x`` and ``Python 3.5.x``. In future it will be available on ``Mac OS``.

https://nexus.soramitsu.co.jp/repository/pypi-develop/ - developer wheels (builds from ``develop`` branch)
https://nexus.soramitsu.co.jp/repository/pypi-release/ - release wheels (builds from ``master`` branch)
https://nexus.soramitsu.co.jp/repository/pypi-develop-nightly/ - dev nightly wheels (daily builds from ``develop`` branch)


Pulling from PyPI
---------------

You can download our PyPI package from our Nexus server (instead of pypi.org). To do this, please specify ``-i`` parameter:

From develop

.. code-block:: shell

    pip install -i https://nexus.soramitsu.co.jp/repository/pypi-develop/ iroha 


From release

.. code-block:: shell

    pip install -i https://nexus.soramitsu.co.jp/repository/pypi-release/ iroha 


From develop-nightly

.. code-block:: shell

    pip install -i https://nexus.soramitsu.co.jp/repository/pypi-develop-nightly/ iroha 

Pushing to PyPI
---------------

This method allows pushing to repo (only for contributors). Twine package is required.

To develop

.. code-block:: shell

    twine upload --repository-url https://nexus.soramitsu.co.jp/repository/pypi-develop/ /path/to/iroha/wheel/iroha.whl


To release

.. code-block:: shell

    twine upload --repository-url https://nexus.soramitsu.co.jp/repository/pypi-release/ /path/to/iroha/wheel/iroha.whl


To develop-nightly

.. code-block:: shell

    twine upload --repository-url https://nexus.soramitsu.co.jp/repository/pypi-develop-nightly/ /path/to/iroha/wheel/iroha.whl

Adding iroha PyPI repositories
---------------
You can simply add our Nexus server of Python packages distribution. 
For this purpose, create file ``.pypirc`` in your home directory:

.. code-block:: shell

    touch ~/.pypirc

Then paste code below:

.. code-block:: shell

    [distutils]
    index-servers=
        iroha-develop
        iroha-release
        iroha-develop-nightly

    [iroha-develop]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-develop/
    [iroha-release]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-release/
    [iroha-develop-nightly]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-develop-nightly/

Save it and exit. Now you can download packages by this command:

.. code-block:: shell

    pip install -r iroha-develop iroha

And push it: 

.. code-block:: shell

    twine upload -r iroha-develop iroha

Where ``iroha-develop`` - repository name
