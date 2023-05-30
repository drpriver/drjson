from setuptools import setup, find_packages, Extension
import sys
import os
if sys.platform == 'win32':
    # hack to compile with clang
    # pr to add clang cl was never merged to distutils
    # https://github.com/pypa/distutils/pull/7
    import distutils._msvccompiler
    class ClangCl(distutils._msvccompiler.MSVCCompiler):
        def initialize(self):
            super().initialize()
            self.cc = 'clang-cl.exe'
            self.compile_options.append('-Wno-unused-variable')
            self.compile_options.append('-Wno-visibility')
            self.compile_options.append('-D_CRT_SECURE_NO_WARNINGS=1')
    distutils._msvccompiler.MSVCCompiler = ClangCl

extension = Extension(
    'drjson.drjson',
    sources = ['pydrjson.c'],
    include_dirs=['..']
)
with open('README-pypi.md', encoding='utf-8') as fp:
    LONG_DESCRIPTION = fp.read()

setup(
    name='drjson',
    version='2.0.0',
    license='Proprietary',
    description='fast json parsing',
    long_description=LONG_DESCRIPTION,
    long_description_content_type='text/markdown',
    # url='',
    author='David Priver',
    author_email='david@davidpriver.com',
    classifiers=[
        # 3 - Alpha
        # 4 - Beta
        # 5 - Production/Stable
        'Development Status :: 4 - Beta',
        'License :: Other/Proprietary License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3 :: Only',
    ],
    packages=['drjson'],  # Required
    ext_modules = [extension],
    python_requires='>=3.6, <4',
    package_data={
        'drjson': ['py.typed', 'drjson.pyi'],
    },
)
