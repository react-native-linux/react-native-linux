// Linux registers no native detector component. The web detector attaches the hook API's handlers to the view
// itself, and the handlers are the Linux engine's, which RNGestureHandlerModule.linux created.
export { default } from './HostGestureDetector.web';
