// Fill out your copyright notice in the Description page of Project Settings.


#include "UDSTCoreTimeLibrary.h"

FString UUDSTCoreTimeLibrary::GetTimeString()
{
    FDateTime Now = FDateTime::Now();
    return FString::Printf(TEXT("%02d:%02d:%02d"),
        Now.GetHour(),
        Now.GetMinute(),
        Now.GetSecond());
}

FString UUDSTCoreTimeLibrary::GetDateString()
{
    FDateTime Now = FDateTime::Now();
    return FString::Printf(TEXT("%04d-%02d-%02d"),
        Now.GetYear(),
        Now.GetMonth(),
        Now.GetDay());
}

FString UUDSTCoreTimeLibrary::GetWeekdayString()
{
    switch (FDateTime::Now().GetDayOfWeek())
    {
    case EDayOfWeek::Monday:    return TEXT("星期一");
    case EDayOfWeek::Tuesday:   return TEXT("星期二");
    case EDayOfWeek::Wednesday: return TEXT("星期三");
    case EDayOfWeek::Thursday:  return TEXT("星期四");
    case EDayOfWeek::Friday:    return TEXT("星期五");
    case EDayOfWeek::Saturday:  return TEXT("星期六");
    case EDayOfWeek::Sunday:    return TEXT("星期日");
    default: return TEXT("");
    }
}
