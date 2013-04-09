FreeRTOS-based I2C Driver for the NXP LPC2103
=============================================

The "old" files are the prior version of the I2C and I2C_ISR that worked
on a prior project. They are included as a reference and provide a
contrast for minimization of the code that gets executed in the ISR. The
[i2c_transaction_summary.pdf](i2c_transaction_summary.pdf)
is a good companion when trying to understand the state transitions
that the I2C controller goes through to execute a transaction.
