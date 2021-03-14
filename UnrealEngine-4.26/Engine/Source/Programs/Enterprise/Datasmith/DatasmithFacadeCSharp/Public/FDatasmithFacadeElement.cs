// Copyright Epic Games, Inc. All Rights Reserved.

//------------------------------------------------------------------------------
// <auto-generated />
//
// This file was automatically generated by SWIG (http://www.swig.org).
// Version 4.0.1
//
// Do not make changes to this file unless you know what you are doing--modify
// the SWIG interface file instead.
//------------------------------------------------------------------------------


public class FDatasmithFacadeElement : global::System.IDisposable {
  private global::System.Runtime.InteropServices.HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal FDatasmithFacadeElement(global::System.IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new global::System.Runtime.InteropServices.HandleRef(this, cPtr);
  }

  internal static global::System.Runtime.InteropServices.HandleRef getCPtr(FDatasmithFacadeElement obj) {
    return (obj == null) ? new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero) : obj.swigCPtr;
  }

  ~FDatasmithFacadeElement() {
    Dispose(false);
  }

  public void Dispose() {
    Dispose(true);
    global::System.GC.SuppressFinalize(this);
  }

  protected virtual void Dispose(bool disposing) {
    lock(this) {
      if (swigCPtr.Handle != global::System.IntPtr.Zero) {
        if (swigCMemOwn) {
          swigCMemOwn = false;
          DatasmithFacadeCSharpPINVOKE.delete_FDatasmithFacadeElement(swigCPtr);
        }
        swigCPtr = new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero);
      }
    }
  }

  // Abandon ownership of the returned FDatasmithFacadeElement C++ pointer.
  internal static global::System.Runtime.InteropServices.HandleRef getCPtrAndDisown(FDatasmithFacadeElement obj)
  {
    if (obj != null)
    {
      obj.swigCMemOwn = false;
    }
    return getCPtr(obj);
  }

  public static string GetStringHash(string Input)
  {
    System.Text.StringBuilder sb = new System.Text.StringBuilder(32);
    InternalGetStringHash(Input, sb, (ulong)sb.Capacity + 1);
    return sb.ToString();
  }

  public static void SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType InWorldCoordinateSystemType) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_SetCoordinateSystemType((int)InWorldCoordinateSystemType);
  }

  public static void SetWorldUnitScale(float InWorldUnitScale) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_SetWorldUnitScale(InWorldUnitScale);
  }

  private static void InternalGetStringHash(string InString, System.Text.StringBuilder OutBuffer, ulong BufferSize) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_InternalGetStringHash(InString, OutBuffer, BufferSize);
  }

  public void SetName(string InElementName) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_SetName(swigCPtr, InElementName);
  }

  public string GetName() {
    string ret = global::System.Runtime.InteropServices.Marshal.PtrToStringUni(DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_GetName(swigCPtr));
    return ret;
  }

  public void SetLabel(string InElementLabel) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_SetLabel(swigCPtr, InElementLabel);
  }

  public string GetLabel() {
    string ret = global::System.Runtime.InteropServices.Marshal.PtrToStringUni(DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeElement_GetLabel(swigCPtr));
    return ret;
  }

  public enum ECoordinateSystemType {
    LeftHandedYup,
    LeftHandedZup,
    RightHandedZup
  }

}
