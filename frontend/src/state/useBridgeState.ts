import { useEffect, useState } from 'react';

import { parseBridgeState } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';

export function useBridgeState(bridge: ChatBridgeClient): BridgeState {
  const [state, setState] = useState(() => parseBridgeState(bridge.currentStateJson()));

  useEffect(() => bridge.subscribe(setState), [bridge]);

  return state;
}
