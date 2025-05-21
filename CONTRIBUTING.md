# Contributing

First off, thanks for taking the time to contribute!

Found a bug, typo, missing feature or a description that doesn't make sense or needs clarification?  
Great, please let us know!

Please note that this project is released with a [Contributor Code of Conduct](CODE-OF-CONDUCT.md). By participating in this project you agree to abide by its terms.

### Bug Reports :bug:

If you find a bug, please search for it first in the [GitHub issues](https://github.com/unfoldedcircle/ucd3-firmware/issues),
and if it isn't already tracked, [create a new issue](https://github.com/unfoldedcircle/ucd3-firmware/issues/new).

### Pull Requests

**Any pull request needs to be reviewed and approved by the Unfolded Circle development team.**

We love contributions from everyone.

⚠️ If you plan to make functional changes or add new features, we kindly ask you, that you please reach out to us first.  
The preferred way for firmware changes is to open a feature request or enhancement with your proposed changes, rather than
directly submitting a pull request, which we'll probably have to decline.

Since this software is being used on the Dock 3 devices, we have to make sure it remains
compatible with the [Dock-API](https://github.com/unfoldedcircle/core-api/tree/main/dock-api) and runs smoothly.

With that out of the way, here's the process of creating a pull request and making sure it passes the automated tests:

### Contributing Code :bulb:

1. Fork the repo.

2. Make your changes or enhancements (preferably on a feature-branch).

   Contributed code must be licensed under the GNU General Public License v3.0 or later.  
   It is required to add a boilerplate copyright notice to the top of each file:

    ```
    // SPDX-FileCopyrightText: Copyright (c) {year} {person OR org} <{email}>
    // SPDX-License-Identifier: GPL-3.0-or-later
    ```

3. Run `./code_style.sh` on your code to ensure the whole codebase has consistent indentation and follows the [code guidelines](doc/code-guidelines.md).

4. Make sure your changes pass the unit tests.  
   See [test](test/) directory for more information. 

5. Push to your fork.

6. Submit a pull request.

At this point we will review the PR and give constructive feedback.  
This is a time for discussion and improvements, and making the necessary changes will be required before we can
merge the contribution.

### Feedback :speech_balloon:

There are a few different ways to provide feedback:

- [Create a new issue](https://github.com/unfoldedcircle/ucd3-firmware/issues/new)
- [Reach out to us on Twitter](https://twitter.com/unfoldedcircle)
- [Visit our community forum](http://unfolded.community/)
- [Chat with us in our Discord channel](http://unfolded.chat/)
- [Send us a message on our website](https://unfoldedcircle.com/contact)
