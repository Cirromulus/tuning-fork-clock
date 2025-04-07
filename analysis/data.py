TABLE_NAME = 'logdata'

class ColDesc:
    def __init__(self, name, type, unit, fractional = 1):
        self.name = name
        self.type = type
        self.unit = unit
        self.fractional = fractional

    def getSql(self) -> str:
        return f"\'{self.name}' {self.type}"

    def normalize(self, value) -> float:
        return value * self.fractional

    def denormalize(self, value) -> float:
        return value * (1 / self.fractional)

    def __str__(self):
        return f"{self.name}: {self.type}, factor: {self.fractional} {self.unit}"

# "oversampling"
expected_frequency = 1 / 440    # should be the same value as in config.hpp!

TABLE_FORMAT = {
    'period': ColDesc('Period', 'INTEGER', 'us', expected_frequency),
    'temperature': ColDesc('Temperature', 'INTEGER', 'Degree Celsius', 0.01),
    'pressure': ColDesc('Pressure', 'INTEGER', 'Pa', pow(2,-8)),
    'humidity': ColDesc('Humidity', 'INTEGER', '%RH', pow(2,-10)),
    'temperature_damped' : ColDesc('TempDamp', 'INTEGER', 'Degree Celsius', 0.01),
    'period_estimate' : ColDesc('PeriodEstimate', 'INTEGER', 'us', expected_frequency),
    'time_estimate' : ColDesc('TimeEstimate', 'INTEGER', 'us', 1),
    'estimate_diff' : ColDesc('EstimateDiff', 'INTEGER', 'us', 1),
}

