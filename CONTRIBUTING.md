# Contributing

First off, thanks for taking the time to contribute!

Found a bug, typo, missing feature, or a description that doesn't make sense or needs clarification?  
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

2. Make your changes or enhancements (preferably on a feature-branch, see best practices below).

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

### Pull Request Best Practices

To ensure efficient review and maintain code quality, please follow these guidelines:

- **One feature per pull request**: Each PR should address a single feature, bug fix, or improvement. This makes reviews
  faster and reduces the risk of introducing bugs. If you have multiple unrelated changes, please submit them as
  separate pull requests.

- **Clean commit history**: Before submitting, rebase or squash your commits to create a clean, logical history. Each
  commit should represent a meaningful, atomic change with a clear commit message. Avoid merge commits in your PR
  branch.

- **Test before submitting**: Only open a pull request once you have thoroughly tested your changes locally. Ensure all
  [unit tests](test/README.md) pass and lints are clean (`./code_style.sh`). **Pull requests with failing automated
  tests will not be reviewed** until they pass.

- **Draft pull requests for early feedback**: If your work is still in progress and you're seeking early feedback, you
  may open a draft pull request. However, please only do this **upon prior agreement** with the maintainers to ensure
  reviewers have availability for interim reviews. Use the draft status to clearly indicate the work-in-progress nature.

- **No work-in-progress after opening (non-draft)**: Once you open a regular (non-draft) pull request, consider it
  complete. Do not continue adding new features or making unrelated changes. If additional work is needed based on
  review feedback, address only those specific points. For new features, create a new branch and PR.

- **Descriptive titles and descriptions**: Use clear, concise PR titles and provide a detailed description explaining
  what changes were made, why they were needed, and any relevant context or testing performed.

### AI-Assisted Contributions :robot:

The use of AI coding assistants and Large Language Models (LLMs) is permitted, **but must be disclosed and used
responsibly**. We follow a "human-in-the-loop" policy inspired by industry best practices:

- **Disclosure required**: If you used AI tools (GitHub Copilot, Cursor, Claude, ChatGPT, etc.) to generate or assist
  with any portion of your contribution, you **must disclose this** in your pull request description. Specify which
  files or sections were AI-assisted.

- **You are responsible**: As the contributor, you are fully responsible for all code you submit, regardless of whether
  AI was used. This means you must:
   - Understand every line of code you submit
   - Have thoroughly tested all AI-generated code
   - Be able to explain and defend design decisions during code review
   - Fix any issues that arise from AI-generated code

- **No "AI slop"**: Low-quality, unreviewed, or poorly understood AI-generated contributions will be **rejected without
  review**. This includes:
   - Code that appears to be generated without human understanding
   - Excessive or unnecessary changes generated by AI agents
   - Code with incorrect assumptions about the codebase
   - Documentation or comments that are generic or inaccurate

- **Human review mandatory**: All AI-assisted code must be carefully reviewed, tested, and validated by you before
  submission. Do not submit code you cannot explain or maintain.

**Example disclosure statement for PR description:**

AI Assistance Disclosure: I used GitHub Copilot to assist with "specific function/file". All code has been reviewed,
tested, and I understand the implementation fully.

### Feedback :speech_balloon:

There are a few different ways to provide feedback:

- [Create a new issue](https://github.com/unfoldedcircle/ucd3-firmware/issues/new)
- [Reach out to us on Twitter](https://twitter.com/unfoldedcircle)
- [Visit our community forum](http://unfolded.community/)
- [Chat with us in our Discord channel](http://unfolded.chat/)
- [Send us a message on our website](https://unfoldedcircle.com/contact)
