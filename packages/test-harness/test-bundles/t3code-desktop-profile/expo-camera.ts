interface PermissionResponse {
  readonly canAskAgain: boolean;
  readonly expires: "never";
  readonly granted: boolean;
  readonly status: "denied";
}

const deniedPermission: PermissionResponse = { canAskAgain: false, expires: "never", granted: false, status: "denied" };

const denied = async (): Promise<PermissionResponse> => await Promise.resolve(deniedPermission);

/**
 * The desktop feature profile's `expo-camera` (docs/research/t3code-linux.md §2): QR pairing is left out with its
 * native module, ExpoCamera. Camera permission reads denied and cannot be asked again, so T3 Code's pairing screen
 * never mounts `CameraView` and pairs through the Address and Code fields it always shows. "Tap to scan" then shows
 * its "Camera access needed" alert, which draws nothing until the Linux `Alert` (#262) lands, and whose Open Settings
 * rejects on Linux.
 */
const useCameraPermissions = (): readonly [PermissionResponse, typeof denied, typeof denied] => [
  deniedPermission,
  denied,
  denied,
];

const CameraView = (): null => null;

export { CameraView, useCameraPermissions };
