// SPDX-License-Identifier: GPL-2.0-only
'use strict';

const managers = [
  {name: 'apt', path: '/usr/bin/apt-get', steps: packages =>
    [['update'], ['install', '-y', '--no-install-recommends', ...packages]]},
  {name: 'dnf', path: '/usr/bin/dnf', steps: packages =>
    [['install', '-y', ...packages]]},
  {name: 'zypper', path: '/usr/bin/zypper', steps: packages =>
    [['--non-interactive', 'install', ...packages]]},
  {name: 'pacman', path: '/usr/bin/pacman', steps: packages =>
    [['-Sy', '--noconfirm', '--needed', ...packages]]},
  {name: 'apk', path: '/sbin/apk', steps: packages =>
    [['add', '--no-cache', ...packages]]},
  {name: 'apk', path: '/usr/bin/apk', steps: packages =>
    [['add', '--no-cache', ...packages]]},
];

function plan({wsl, distro, wslExe, pkexec, manager, packages}) {
  if (!manager || !managers.some(item => item.path === manager.path))
    throw Error('This Linux distribution has no supported graphical package handoff. Choose a desktop distribution or a WSL2 distribution with apt, dnf, zypper, pacman or apk.');
  if (!Array.isArray(packages) || !packages.length ||
      packages.some(item => !['git', 'clang', 'zstd'].includes(item)))
    throw Error('There are no supported computer tools to install.');
  let prefix;
  if (wsl) {
    if (typeof distro !== 'string' || !/^[^\x00-\x1f\x7f]{1,100}$/.test(distro) || !wslExe)
      throw Error('The selected WSL2 distribution cannot be identified. Reopen Moader Setup from the Windows launcher.');
    prefix = [wslExe, '-d', distro, '-u', 'root', '--'];
  } else {
    if (!pkexec)
      throw Error('No graphical administrator prompt is available here. Open this desktop’s Software app to install the named tools, then retry preparation.');
    prefix = [pkexec];
  }
  return manager.steps(packages).map((args, index) => ({
    exe: prefix[0], args: [...prefix.slice(1), manager.path, ...args],
    label: index === 0 && args[0] === 'update'
      ? 'Refreshing signed Linux package indexes' :
        'Installing missing Linux tools: ' + packages.join(', '),
  }));
}

module.exports = {managers, plan};
