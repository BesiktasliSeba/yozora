#include "config.h"
#include "NetworkStateNotifier.h"

namespace WebCore {

void NetworkStateNotifier::updateStateWithoutNotifying()
{
    // ios 5 has no reachability source here, failed requests report their own network error
    m_isOnLine = true;
}

void NetworkStateNotifier::startObserving()
{
    // nothing changes the state above, so there is nothing to observe
}

}
