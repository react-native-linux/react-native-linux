import { createContext, createElement, useContext, useEffect, useSyncExternalStore } from "react";
// oxlint-disable-next-line import/no-relative-parent-imports -- src-linux mirrors upstream's Libraries tree, which imports across it.
import { mountAlertRoot, readAlertRequests, subscribeAlertRequests } from "../Alert/alertRequests.ts";
// oxlint-disable-next-line import/no-relative-parent-imports -- src-linux mirrors upstream's Libraries tree, which imports across it.
import AlertModal from "../Alert/AlertModal.tsx";
import type { ComponentType } from "react";
import type { RootTag } from "react-native";
import { RootTagContext } from "react-native";

interface AppContainerProps {
  readonly rootTag: number;
  readonly [prop: string]: unknown;
}

// oxlint-disable-next-line eslint/no-underscore-dangle -- Metro's global, named by React Native.
declare const __DEV__: boolean;

/* oxlint-disable typescript/no-require-imports, typescript/no-var-requires, unicorn/prefer-module -- Upstream's own selection: a production bundle never loads the development container. */
const UpstreamAppContainer: ComponentType<AppContainerProps> = __DEV__
  ? require("react-native/Libraries/ReactNative/AppContainer-dev").default
  : require("react-native/Libraries/ReactNative/AppContainer-prod").default;
/* oxlint-enable typescript/no-require-imports, typescript/no-var-requires, unicorn/prefer-module */

// oxlint-disable-next-line import/no-commonjs, typescript/no-require-imports, typescript/no-var-requires, unicorn/prefer-module -- The one way to make a RootTag, which react-native's types do not declare.
const createRootTag: (rootTag: number) => RootTag = require("react-native/Libraries/ReactNative/RootTag").createRootTag;

const HostedContext = createContext(false);

const doNothing = (): null => null;

/**
 * Upstream's container, and its root's alert host beside it rather than inside the app's `WrapperComponent`, a slot
 * the app owns (ADR-0005). The host draws only this root's requests, newest on top, so no alert is drawn twice. The
 * container a development `Modal` nests shares its root's tag and hosts nothing.
 */
const AppContainer = (props: AppContainerProps): React.JSX.Element => {
  const isNested = useContext(HostedContext);
  const requests = useSyncExternalStore(subscribeAlertRequests, readAlertRequests);
  const { rootTag } = props;

  useEffect(() => (isNested ? doNothing : mountAlertRoot(rootTag)), [isNested, rootTag]);

  return isNested ? (
    createElement(UpstreamAppContainer, props)
  ) : (
    <HostedContext.Provider value>
      {createElement(UpstreamAppContainer, props)}
      <RootTagContext.Provider value={createRootTag(rootTag)}>
        {requests
          .filter((request) => request.rootTag === rootTag)
          .map((request) => (
            <AlertModal key={request.id} request={request} />
          ))}
      </RootTagContext.Provider>
    </HostedContext.Provider>
  );
};

export default AppContainer;
