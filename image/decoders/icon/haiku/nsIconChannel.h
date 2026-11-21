/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_icon_haiku_nsIconChannel_h
#define mozilla_image_decoders_icon_haiku_nsIconChannel_h

#include "mozilla/Attributes.h"

#include "nsIChannel.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIIconURI.h"
#include "nsCOMPtr.h"

/// Haiku implementation of nsIconChannel: renders the Tracker/MIME icon for a
/// file, content type or extension into a BBitmap and serves it as an
/// image/icon stream. All nsIChannel calls are proxied to that stream channel.
class nsIconChannel final : public nsIChannel
{
  public:
    NS_DECL_ISUPPORTS
    NS_FORWARD_NSIREQUEST(mRealChannel->)
    NS_FORWARD_NSICHANNEL(mRealChannel->)

    nsIconChannel() { }

    static void Shutdown() { }

    /// Called by nsIconProtocolHandler after it creates this channel.
    nsresult Init(nsIURI* aURI);
  private:
    ~nsIconChannel() { }
    nsCOMPtr<nsIChannel> mRealChannel;
};

#endif // mozilla_image_decoders_icon_haiku_nsIconChannel_h
