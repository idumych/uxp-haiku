/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuRemote_h
#define mozilla_widget_HaikuRemote_h

namespace mozilla {
namespace widget {

/*
 * Single launch on Haiku (there is no X11 remoting). Called by XRE early,
 * before the profile is locked.
 *
 * Picks this instance's application signature: the default one, or a
 * per-profile one when -P/-profile is given, or a unique one with
 * -no-remote/MOZ_NO_REMOTE. If an instance with that signature is already
 * running, sends it our command line (B_ARGV_RECEIVED, handled by its
 * HaikuApplication through nsICommandLineRunner) and returns true: the
 * caller should then exit quietly.
 */
bool HaikuForwardToRunningInstance(int aArgc, char** aArgv);

}  // namespace widget
}  // namespace mozilla

#endif  // mozilla_widget_HaikuRemote_h
