
#ifndef MT8193_IIC_H
#define MT8193_IIC_H

/*----------------------------------------------------------------------------*/
/* IIC APIs                                                                   */
/*----------------------------------------------------------------------------*/
int mt8193_i2c_read(u16 addr, u32 *data);
int mt8193_i2c_write(u16 addr, u32 data);

#endif /* MT8193_IIC_H */

