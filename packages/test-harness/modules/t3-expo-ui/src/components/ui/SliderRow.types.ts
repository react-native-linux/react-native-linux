export interface SliderRowProps {
  readonly disabled?: boolean;
  readonly icon: string;
  readonly minimumIcon?: string;
  readonly maximumIcon?: string;
  readonly label: string;
  readonly valueLabel: string;
  readonly min: number;
  readonly max: number;
  readonly step: number;
  readonly value: number;
  readonly onChange: (value: number) => void;
}
