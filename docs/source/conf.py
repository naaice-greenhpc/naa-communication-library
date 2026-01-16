# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys

sys.path.insert(0, os.path.abspath("."))


# -- Project information -----------------------------------------------------

project = "naa-communication-library"
copyright = (
    "2025, Florian Mikolajczak, Dylan Everingham, Hannes Signer, Niklas Schelten"
)
author = "Florian Mikolajczak, Dylan Everingham, Hannes Signer, Niklas Schelten"

# The full version, including alpha/beta/rc tags
release = "0.1"


# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    "breathe",
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.viewcode",
    "sphinx.ext.intersphinx",
]

breathe_projects = {
    "naa-communication-prototype": "../doxygen/xml"  # Pfad zur Doxygen-XML-Ausgabe
}
breathe_default_project = "naa-communication-prototype"

# exhale_args = {
#     "containmentFolder": "./api",
#     "rootFileName": "library_root.rst",
#     "rootFileTitle": "API Reference",
#     "doxygenStripFromPath": "..",
#     "createTreeView": True,
# }

# html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
templates_path = ["_templates"]

# Add any paths that contain templates here, relative to this directory.
templates_path = ["_templates"]

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = []


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = "sphinx_rtd_theme"
html_logo = "_static/naaice.png"
html_theme_options = {"logo_only": True}

html_search_language = "en"


# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ["_static"]
html_favicon = "_static/naaice.png"
