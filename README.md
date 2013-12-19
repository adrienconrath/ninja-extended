ninja-extended
==============

Hacking the Ninja build system to add new wacky functionalities:

- A daemon is notified of file modifications so that the partial directed acyclic graph to be rebuilt is bottom-up
  computed;

- Stale files due to target being renamed are deleted;

- Targets that only depend on up-to-date versioned files are retrieved from a shared Key-Value store instead of being
  built locally.

This is a WIP, nothing is working yet!
