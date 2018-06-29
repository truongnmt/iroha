Python library
==============


About Nexus
---------------
https://nexus.soramitsu.co.jp/ - our new PyPI, Maven, artifacts server with precompilled librares.
You can download binaries without compilation on the client side. It helps to save you time and disk space for installing dependencies like libboost, libz, etc.

Now our packages are distributed only for ``Windows x86_64`` with ``Python 2.7.x`` and ``Python 3.5.x``. In future it will be available on ``manylinux`` platform and ``Mac OS``.

https://nexus.soramitsu.co.jp/repository/pypi-dev/ - developer wheels
https://nexus.soramitsu.co.jp/repository/pypi-release/ - release wheels
https://nexus.soramitsu.co.jp/repository/pypi-dev-nightly/ - dev nightly wheels
https://nexus.soramitsu.co.jp/repository/pypi-release-nightly/ - release nightly wheels


Pulling from PyPI
---------------

You can download our PyPI package from our Nexus server (instead of pypi.org). To do this, please specify ``-i`` parameter:

.. code-block:: shell

    pip install -i https://nexus.soramitsu.co.jp/repository/pypi-{develop,release,develop-nightly,release-nightly}/ iroha 

Pushing to PyPI
---------------

This method allows pushing to repo (only for contributors). Twine package is required.

.. code-block:: shell

    twine upload --repository-url https://nexus.soramitsu.co.jp/repository/pypi-{develop,release,develop-nightly,release-nightly}/ /path/to/iroha/wheel/iroha.whl

Adding iroha PyPI repositories
---------------
You can simple add our Nexus server of Python packages distribution. 
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
        iroha-release-nightly

    [iroha-develop]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-dev/
    [iroha-release]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-release/
    [iroha-develop-nightly]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-dev-nightly/
    [iroha-release-nightly]
    repository: https://nexus.soramitsu.co.jp/repository/pypi-release-nightly/

Save it and exit. Now you can download packages by this command:

.. code-block:: shell

    pip install -r iroha-develop iroha

And push it: 

.. code-block:: shell

    twine upload -r iroha-develop iroha

Where ``iroha-develop`` - repository name